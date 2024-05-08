/// Generic imports
#include <Wire.h>
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

// Set global mode
// - PROD - does not send all results, higher sample rate
// - DEBUG - sends all results on second BLE characteristic, lowers sample rate
#define MODE "PROD"
// #define MODE "DEBUG"

// I2C bus and serial port
#define DEV_I2C Wire
#define SerialPort Serial

// I2C Multiplexer
#include <SparkFun_PCA9846.h>
SparkFun_PCA9846 myMux;
#define PORT_COUNT 4

// ToF Distance Sensor(s)
#include <vl53l4cd_class.h>
VL53L4CD sensor_vl53l4cd_sat(&DEV_I2C, A1);

// Distance sensor parameters
uint32_t timing_budget_ms = 10;
uint32_t inter_measurement_ms = 0;

// For getting running averages
#include <RunningAverage.h>
#define SMOOTHING_WINDOW_SIZE 4
#define NUM_BUFFERS 8

// CIRCULAR BUFFER STUFF FOR MOVING AVERAGES
// Arrays to hold recent samples, for moving average circular buffers
uint16_t sr_history_0[SMOOTHING_WINDOW_SIZE];
uint16_t sr_history_1[SMOOTHING_WINDOW_SIZE];
uint16_t sr_history_2[SMOOTHING_WINDOW_SIZE];
uint16_t sr_history_3[SMOOTHING_WINDOW_SIZE];
uint16_t ar_history_0[SMOOTHING_WINDOW_SIZE];
uint16_t ar_history_1[SMOOTHING_WINDOW_SIZE];
uint16_t ar_history_2[SMOOTHING_WINDOW_SIZE];
uint16_t ar_history_3[SMOOTHING_WINDOW_SIZE];
// Pointer to history buffers
uint16_t* buffers[NUM_BUFFERS] = { sr_history_0, sr_history_1, sr_history_2, sr_history_3, ar_history_0, ar_history_1, ar_history_2, ar_history_3 };
int rolling_average_idx = 0;                      // Index counter
uint32_t sr_totals[PORT_COUNT];  // Array to hold signal_rate_kcps totals
uint32_t ar_totals[PORT_COUNT];  // Array to hold ambient_rate_kcps totals
float sr_averages[PORT_COUNT];   // Array to hold signal_rate_kcps averages
float ar_averages[PORT_COUNT];   // Array to hold ambient_rate_kcps_totals
// Make all values in arrays 0
#define NORMALISED_ERROR_VALUE 255

// Calibration values
uint16_t calib_lowerBounds[4]       = { 2000,   2000,   2000,   2000  };  // Lower bounds (lowest expected raw values)
uint16_t calib_Ranges[4]            = { 25000,  29000,  17000,  23000 };  // Ranges (above the lowest expected raw values)
uint16_t calib_sigmaThresholds[4]   = { 5, 5, 5, 5 };                     // Sigma thresholds
uint16_t calib_signalThresholds[4]  = { 4096, 4096, 4096, 4096 };         // Signal thresholds

// LED
#ifndef LED_BUILTIN
  #define LED_BUILTIN 13
#endif
#define LedPin LED_BUILTIN

// BLE
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLEServer* pServer = NULL;
BLECharacteristic* pReadingsCharacteristic = NULL;
BLECharacteristic* pDebugCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define READINGS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define DEBUG_CHARACTERISTIC_UUID "1e0c94ff-b7ce-4e72-8977-9a1f8ad4cc2a"

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

void initDistanceSensor(uint8_t port) {
  myMux.setPort(port);

  // Configure VL53L4CD satellite component
  sensor_vl53l4cd_sat.begin();

  // Switch off VL53L4CD satellite component
  sensor_vl53l4cd_sat.VL53L4CD_Off();

  //Initialize VL53L4CD satellite component
  sensor_vl53l4cd_sat.InitSensor();

  // Set ranging parameters for the sensors
  sensor_vl53l4cd_sat.VL53L4CD_SetRangeTiming(timing_budget_ms, inter_measurement_ms);

  // Set mm distance thresholds?
  sensor_vl53l4cd_sat.VL53L4CD_SetDetectionThresholds(200, 300, 0);

  // Set sigma threshold?
  sensor_vl53l4cd_sat.VL53L4CD_SetSigmaThreshold(calib_sigmaThresholds[port]);

  // Set signal thresholds?
  sensor_vl53l4cd_sat.VL53L4CD_SetSignalThreshold(calib_signalThresholds[port]);

  // Start measurements
  sensor_vl53l4cd_sat.VL53L4CD_StartRanging();
}

void initRunningAverageBuffers() {
  for (int i = 0; i < SMOOTHING_WINDOW_SIZE; i++) {
    sr_history_0[i] = 0;
    sr_history_1[i] = 0;
    sr_history_2[i] = 0;
    sr_history_3[i] = 0;
    ar_history_0[i] = 0;
    ar_history_1[i] = 0;
    ar_history_2[i] = 0;
    ar_history_3[i] = 0;
  }

  memset(sr_totals, 0, sizeof(sr_totals));
  memset(ar_totals, 0, sizeof(ar_totals));
  memset(sr_averages, 0.0, sizeof(sr_averages));
  memset(ar_averages, 0.0, sizeof(ar_averages));
}

void setup() {

  // Init built-in LED pin
  pinMode(LedPin, OUTPUT);

  // Start serial port, I2C bus, and multiplexer
  Serial.begin(115200);
  DEV_I2C.begin();
  myMux.begin();

  // Create BLE device
  BLEDevice::init("BowTracker");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic for first distance sensor
  pReadingsCharacteristic = pService->createCharacteristic(
    READINGS_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_INDICATE);

  pDebugCharacteristic = pService->createCharacteristic(
    DEBUG_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_INDICATE);

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create BLE Descriptors
  pReadingsCharacteristic->addDescriptor(new BLE2902());
  pDebugCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");

  // Init all distance sensors
  for (uint8_t port = 0; port < PORT_COUNT; port++) { initDistanceSensor(port); }

  // Init arrays for running averages
  initRunningAverageBuffers();
}

void loop() {

  // Handling connecting or disconnecting BLE devices
  // On disconnect
  if (!deviceConnected && oldDeviceConnected) {

    digitalWrite(LedPin, LOW);    // Turn off LED
    delay(500);                   // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising();  // restart advertising
    Serial.println("DISCONNECTED. Restarting advertising...");
    oldDeviceConnected = deviceConnected;
  }

  // On connect
  if (deviceConnected && !oldDeviceConnected) {
    // do stuff here on connecting
    Serial.println("CONNECTED. Starting notify.");
    oldDeviceConnected = deviceConnected;
  }

  // If already connected, get sensor values, update characteristics, etc.
  if (deviceConnected) {

    int readings[4] = { 65535, 65535, 65535, 65535 }; // Array to hold distance values
    float normalisedReadings[4] = { -1, -1, -1, -1 }; // Array to hold normalised readings

    // Init array to hold raw results from all sensors
    uint16_t debugSensorResults[32];

    // Turn on LED
    digitalWrite(LedPin, HIGH);

    // Iterate through all sensors
    for (uint8_t port = 0; port < PORT_COUNT; port++) {
      // Update connected I2C port
      myMux.setPort(port);

      // Init variables
      uint8_t NewDataReady = 0;
      VL53L4CD_Result_t results;
      uint8_t status;
      char report[64];

      // Check for whether a new value is available
      do {
        status = sensor_vl53l4cd_sat.VL53L4CD_CheckForDataReady(&NewDataReady);
      } while (!NewDataReady);

      // Get data from distance sensor
      if ((!status) && (NewDataReady != 0)) {
        // (Mandatory) Clear HW interrupt to restart measurements
        sensor_vl53l4cd_sat.VL53L4CD_ClearInterrupt();

        // Read measured distance. RangeStatus = 0 means valid data
        sensor_vl53l4cd_sat.VL53L4CD_GetResult(&results);

        // Update appropriate BLE characteristic with sensor value
        // If valid measurement (range_status == 0)
        if (results.range_status == 0) {

          // Update readings array
          readings[port] = results.distance_mm;

          // Rolling average handling
          // Create pointers to relevant buffers
          uint16_t* tempSrBuffer = buffers[port];               // Signal rate buffer
          uint16_t* tempArBuffer = buffers[port + PORT_COUNT];  // Ambient rate buffer
          // Init variables for holding averages of each value for this sensor
          float srAverage = 0;
          float arAverage = 0;
          // Init float for holding normalised reading
          // Error value, so can check later if not updated to proper value
          float normalisedReading = NORMALISED_ERROR_VALUE;
          // Subtract previous readings from totals
          sr_totals[port] = sr_totals[port] - tempSrBuffer[rolling_average_idx];
          ar_totals[port] = ar_totals[port] - tempArBuffer[rolling_average_idx];
          // Update circular buffers with new samples
          tempSrBuffer[rolling_average_idx] = results.signal_rate_kcps / results.number_of_spad;
          tempArBuffer[rolling_average_idx] = results.ambient_rate_kcps / results.number_of_spad;
          // Update totals with new readings
          sr_totals[port] = sr_totals[port] + tempSrBuffer[rolling_average_idx];
          ar_totals[port] = ar_totals[port] + tempArBuffer[rolling_average_idx];
          // Calulcate averages for current sensor, after having updated the relevant circular buffers
          srAverage = sr_totals[port] / SMOOTHING_WINDOW_SIZE;
          arAverage = ar_totals[port] / SMOOTHING_WINDOW_SIZE;
          // Calculate normalised reading and constrain range
          normalisedReading = ((srAverage - arAverage) - calib_lowerBounds[port]) / calib_Ranges[port];
          if (normalisedReading != NORMALISED_ERROR_VALUE) {       // Error value - leave in place for detection on PC-side
            if (normalisedReading > 1) { normalisedReading = 1; }  // Above range
            else if (normalisedReading < 0) {
              normalisedReading = 0;
            }  // Below range
          }
          // Add normalisedReading to array holding the normalised values for this loop
          normalisedReadings[port] = normalisedReading;
          // Print normalised reading to Serial port
          Serial.print("NR");
          Serial.print(port);
          Serial.print(":");
          Serial.println(normalisedReading);

          // DEBUG MODE ONLY: send all results as single array, with each 8 items being from subsequent sensors
          if (MODE == "DEBUG") {
            // Add raw results to array
            debugSensorResults[(port * 8) + 0] = results.range_status;
            debugSensorResults[(port * 8) + 1] = results.distance_mm;
            debugSensorResults[(port * 8) + 2] = results.ambient_rate_kcps;
            debugSensorResults[(port * 8) + 3] = results.ambient_per_spad_kcps;
            debugSensorResults[(port * 8) + 4] = results.signal_rate_kcps;
            debugSensorResults[(port * 8) + 5] = results.signal_per_spad_kcps;
            debugSensorResults[(port * 8) + 6] = results.number_of_spad;
            debugSensorResults[(port * 8) + 7] = results.sigma_mm;
          }

        } else {
          Serial.print("ERROR: range status ");
          Serial.println(results.range_status);
        }
      }
    }

    // Normalised readings
    // Covert normalisedReadings array to buffer
    uint8_t buf[16];
    memcpy(buf, &normalisedReadings, sizeof(normalisedReadings));

    // Update readings characteristic from buffer
    pReadingsCharacteristic->setValue(buf, sizeof(buf));
    pReadingsCharacteristic->notify();
    delay(3);

    // Raw readings - update characteristic
    if (MODE == "DEBUG") {
      // Covert raw sensor results to buffer
      uint8_t debug_buf[64];
      memcpy(debug_buf, &debugSensorResults, sizeof(debugSensorResults));
      // Update characteristic from buffer
      pDebugCharacteristic->setValue(debug_buf, sizeof(debug_buf));
      pDebugCharacteristic->notify();
      delay(3);
    }

    // Display bound varibles on serial plotter
    Serial.print("upper:");
    Serial.println(1);
    Serial.print("lower:");
    Serial.println(0);

    // Turn off LED
    digitalWrite(LedPin, LOW);

    // Update index once per iteration of loop(), if device is connected to Bluetooth
    rolling_average_idx++;
    if (rolling_average_idx >= SMOOTHING_WINDOW_SIZE) { rolling_average_idx = 0; }
  }

  // // Serial.println();
  // delay(2);
}
