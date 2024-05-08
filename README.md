# bow-tracker

This repo contains:

- The pre-trained bow tracking model, trained to predict bow position and bow force from four distance sensor measurements
- The accompanying Python scripts for implementing the model and handling BLE communication
- The firmware for the ESP32 microcontroller
- The Max patch for dataset collection
- Other utilities for dataset collection, pre-processing, and analysis
- The Jupyter notebook for training and evaluating bow tracking neural networks

This repo does **not** contain:
- The dataset used to train the included bow tracking model

A bill of parts for the bow tracking system is found at `./docs/bow_tracking_bill_of_parts.csv`.

## Running the bow tracking system

The bow tracking system consists of three components:

### ESP32 firmware

Installing the firmware on the ESP32 microcontroller requires the Arduino IDE and packages for your particular version of the ESP32, the PCA9846 I2C multiplexer, and VLC53L4CD optical distance sensors. All of these are available from the built-in Library Manager in the Arduino IDE or on GitHub.

To install the firmware:

Open `./src/ESP32_bow_tracker_firmware/ESP_32_bow_tracker_firmware.ino` in Arduino IDE, compile, and upload to your ESP32.

### Python script

The Python script handles BLE communication between the ESP32 and computer, implements the trained model, and sends both raw distance measurements and predicted bow position and bow force to the Max patch over OSC.

Install the required packages in your Python environment:

``
pip install -r requirements.txt
``

Run the system in PROD mode (for using the trained model):

``
python ./src/main.py
``

The system can also be specified to run in either PROD mode or DATASET mode. Dataset mode is used for collecting datasets, and also retrieves the raw parameters exposed by the VL53L4CD ToF distance sensors:

``
python ./src/main.py <mode: PROD / DATASET>
``

Running in prod mode also requires ESP32 firmware to be set into DEBUG mode, which is done by commenting line 14 and uncommenting line 15 of the firmware source code: `./src/ESP32_bow_tracker_firmware/ESP32_bow_tracker_firmware.ino`.

For PROD mode:

```cpp
    #define MODE "PROD"
    // #define MODE "DEBUG"
```

For DATASET/DEBUG mode:

```cpp
    // #define MODE "PROD"
    #define MODE "DEBUG"
```

### Max patch

Open `./src/bow-tracker_starter_patch.maxpat` (requires [Max 8](https://cycling74.com/products/max)).