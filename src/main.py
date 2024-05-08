import asyncio
from bleak import BleakClient
import signal
from pythonosc.udp_client import SimpleUDPClient
import numpy as np
import struct
import time
from datetime import timedelta
import os
import sys

print("------------------------------")
print("-------- BOW-TRACKER ---------")
print("------------------------------")
print("---- Press ctrl+c to exit ----")
print("------------------------------")

os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3' # Gets TensorFlow to shut up...

# Set mode: "PROD" or "DATASET" from command line argument
if len(sys.argv) == 2:
    if sys.argv[1].upper() == "PROD":
        MODE = sys.argv[1].upper()
    elif sys.argv[1].upper() == "DATASET":
        MODE = sys.argv[1].upper()
    else:
        raise ValueError(f"Expected second argument 'prod/PROD' or 'dataset/DATASET', received '{sys.argv[1]}.")
elif len(sys.argv) > 2:
    raise ValueError(f"Too many arguments. Use 'python ./src/main.py <mode: prod/dataset>, or no arguments to default to 'prod' mode.")
else: # If no command line argument, default to PROD mode
    MODE = "PROD"

print(f"Mode: {MODE}")

# The address of the SparkFun ESP32 Thing Plus C
DEVICE_ADDRESS = "D4:8A:FC:C3:5F:16"

# UUIDs for the service and characteristic
SERVICE_UUID                    = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
READINGS_CHARACTERISTIC_UUID    = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
DEBUG_CHARACTERISTIC_UUID       = "1e0c94ff-b7ce-4e72-8977-9a1f8ad4cc2a"

# OSC client details
IP = "127.0.0.1"
PORT = 12000

MODES = {
    "PROD":     0, 
    "DATASET" : 1
}

# Init counter for number of samples received by each callback
norm_samples_counter = 0
raw_samples_counter  = 0

# Dataset collection inits
if MODES[MODE] == 1:
    # Init array to hold all data together
    merged_sample_data = []

# Boolean for whether collecting dataset values
IS_DATASET_COLLECTING = True if MODES[MODE] == 1 else False

# Boolean for whether to send values to the model
IS_PREDICTING = True if MODES[MODE] == 0 else False

# Load model if predicting
if IS_PREDICTING:
    from bow_tracker import BowTracker
    MODELS_FOLDER_PATH = "./src/trained_model/"
    model_name = "bow_tracker_model"
    model_path = os.path.join(MODELS_FOLDER_PATH, model_name)
    bow_tracker = BowTracker(model_path)
    print(f"Loaded model at: {model_path}")
    print("------------------------------")

async def run_ble_client(address, loop) -> None:
    
    # Create OSC server
    osc_client = SimpleUDPClient(IP, PORT)

    # Create event for neatly handling disconnects
    disconnected_event = asyncio.Event()

    def disconnected_callback(client) -> None:
        disconnected_event.set()

    def raise_graceful_exit(*args) -> None:
        disconnected_event.set()
    
    async with BleakClient(address, loop=loop, disconnected_callback=disconnected_callback) as client:
        
        # Add signal handler for ctrl+c
        signal.signal(signal.SIGINT, raise_graceful_exit)
        signal.signal(signal.SIGTERM, raise_graceful_exit)

        # Connect to the device
        connected = await client.is_connected()
        print(f"Connected: {str(connected).lower()}")
        print("------------------------------")

        def is_valid_dataset_example(merged_sample_data) -> bool:
            if len(merged_sample_data) == 36:
                return True
            return False

        # Updates the list of merged data, i.e., the new row of the dataset
        def update_merged_data(
                merged_sample_data: list, 
                decoded_data: tuple, 
                data_type: str
                ) -> list:
            if data_type == "norm":
                return list(decoded_data)
            elif data_type == "raw":
                # Catches when characteristics get out of sync, will wait for next norm sample to reset
                if len(merged_sample_data) != 4: 
                    raise ValueError(f"Expected 4 values in list, received {len(merged_sample_data)}")
                return merged_sample_data + list(decoded_data)
            else:
                raise ValueError(f"Expected data_type 'norm' or 'raw', received {data_type}")

        # This function called on reading characteristic value update
        def reading_notification_handler(sender, readings_data) -> None:
            # Update counter for how many normalised samples received
            global norm_samples_counter
            norm_samples_counter += 1
            
            # Parse BLE data
            decoded_data = struct.unpack("<4f", readings_data)
            print(f"Received data: {decoded_data}")
            osc_client.send_message("/distances", list(decoded_data))
            
            # Predict using model, if in PROD mode
            if IS_PREDICTING:
                osc_client.send_message("/pred", bow_tracker.predict(decoded_data))

            # Create list for dataset, if in dataset collection mode
            if IS_DATASET_COLLECTING:
                global merged_sample_data
                merged_sample_data = update_merged_data(merged_sample_data, decoded_data, "norm") 
        
        # This function is called on reading debug characteristic value update
        def debug_notification_handler(sender, debug_data) -> None:
            # Update counter for how many raw samples received
            global raw_samples_counter
            raw_samples_counter += 1
            
            # Parse BLE data
            decoded_data = struct.unpack("<32H", debug_data)
            print(f"  DEBUG: {decoded_data}")
            osc_client.send_message("/debug", list(decoded_data))
            
            # Create list for dataset, if in dataset_collection mode
            if IS_DATASET_COLLECTING:
                global merged_sample_data
                merged_sample_data = update_merged_data(merged_sample_data, decoded_data, "raw")
                # Send dataset example over OSC if valid example (i.e., 36 items in list)
                if is_valid_dataset_example(merged_sample_data):
                    osc_client.send_message("/dataset_example", merged_sample_data)

        # Start receiving notifications
        await client.start_notify(READINGS_CHARACTERISTIC_UUID, reading_notification_handler)
        if IS_DATASET_COLLECTING:
            await client.start_notify(DEBUG_CHARACTERISTIC_UUID, debug_notification_handler)
        start = time.time()
        
        print("Subscribed to characteristic updates. Listening...")

        # Keep running until disconnected event is set
        # Either from disconnect or from KeyboardInterrupt
        await disconnected_event.wait()

        # Stop receiving notifications
        stop = time.time()
        await client.stop_notify(READINGS_CHARACTERISTIC_UUID)
        if IS_DATASET_COLLECTING:
            await client.stop_notify(DEBUG_CHARACTERISTIC_UUID)
        print("------------------------------")
        print(f"Unsubscribed and disconnected.")
        print(f"Received {norm_samples_counter} samples in {timedelta(seconds=stop-start)} (avg {round(norm_samples_counter/(stop-start), 3)} Hz)")

def main(address: str) -> None:
    # Start BLE connection and getting data
    loop = asyncio.get_event_loop()
    loop.run_until_complete(run_ble_client(address, loop))

if __name__ == "__main__":
    main(DEVICE_ADDRESS)