#ifdef BT_BLE
#include "bleUart.h"
#endif
#ifdef BT_CLIENT
#include "bleClient.h"
#endif

// Add WiFi header file to support WiFi status checking
#include <WiFi.h>


// Include tools.h to get debug print macro definitions
#include "tools.h"

#ifdef BT_SSP
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif
#endif

// Bluetooth mode management variables and functions
#define BLUETOOTH_MODE_DEFINED
enum BluetoothMode { BT_MODE_NONE = 0, BT_MODE_SERVER = 1, BT_MODE_CLIENT = 2, BT_MODE_BOTH = 3 };

BluetoothMode activeBtMode = BT_MODE_NONE;
unsigned long btModeDecisionStartTime = 0;            // 3 second decision timer
unsigned long btModeLastCheckTime = 0;                // 1 second check interval timer
const unsigned long BT_MODE_CHECK_INTERVAL = 1000;    // Check every second
const unsigned long BT_MODE_DECISION_TIMEOUT = 3000;  // 3 second decision timeout

void initBluetoothModes();
void checkAndSwitchBluetoothMode();
void shutdownBleServer();
void shutdownBleClient();

#ifdef BT_SSP
BluetoothSerial SerialBT;
boolean confirmRequestPending = true;
boolean BTconnected = false;

void BTConfirmRequestCallback(uint32_t numVal) {
  confirmRequestPending = true;
  Serial.print("SSP PIN: ");
  Serial.println(numVal);
  Serial.println("Auto-confirming SSP pairing...");
  SerialBT.confirmReply(true);  // Auto-confirm pairing request
  confirmRequestPending = false;
}

void BTAuthCompleteCallback(boolean success) {
  confirmRequestPending = false;
  if (success) {
    BTconnected = true;
    Serial.println("SSP Pairing success!!");
  } else {
    BTconnected = false;
    Serial.println("SSP Pairing failed, rejected by user!!");
  }
}

void blueSspSetup() {
  SerialBT.enableSSP();
  SerialBT.onConfirmRequest(BTConfirmRequestCallback);
  SerialBT.onAuthComplete(BTAuthCompleteCallback);
  char* sspName = getDeviceName("_SSP");
  PTHL("SSP:\t", sspName);
  SerialBT.begin(sspName);  // Bluetooth device name
  delete[] sspName;         // Free the allocated memory
  Serial.println("The SSP device is started, now you can pair it with Bluetooth!");
}

// end of Richard Li's code
#endif

// Bluetooth mode management function implementation
void initBluetoothModes() {
  PTLF("Initializing Bluetooth modes...");

  // If WiFi is connecting, wait to avoid resource competition
  if (WiFi.status() == WL_DISCONNECTED) {
    PTLF("Waiting for WiFi connection to stabilize before starting Bluetooth...");
    delay(1000);
  }


  btModeDecisionStartTime = millis();  // Start 3 second decision timer
  btModeLastCheckTime = millis();      // Initialize check interval timer
  // PTLF("Both BT modes started. Waiting for connection...");

#ifdef BT_CLIENT
  PTLF("Starting BLE Client...");
  bleClientSetup();
  delay(200);  // Increase startup time, avoid WiFi conflict

  unsigned long currentTime = millis();

  // Check 3 second decision timeout (using independent timer)
  while (currentTime - btModeDecisionStartTime < BT_MODE_DECISION_TIMEOUT) {
    // Reduce scan frequency, avoid excessive WebSocket connection interference
    if (currentTime - btModeLastCheckTime >= BT_MODE_CHECK_INTERVAL) {
      checkBtScan();
      if (btConnected) {
        PTLF("BLE Client connected, shutting down Server mode");
        activeBtMode = BT_MODE_CLIENT;
      }
      btModeLastCheckTime = currentTime;
    }
    // Check WebSocket connection status, if there are active connections reduce scan frequency

    extern bool webServerConnected;
    extern std::map<uint8_t, bool> connectedClients;

    if (webServerConnected && !connectedClients.empty()) {
      delay(500);  // Give WebSocket more time to process
    }

    delay(100);
    currentTime = millis();
  }
  // After timeout, shut down client mode and start server mode
  if (activeBtMode != BT_MODE_CLIENT) {
    PTLF("Shutting down BLE Client...");
    shutdownBleClient();
    delay(500);  // Give BLE stack more time to complete cleanup

    // Completely reinitialize BLE device to switch to Server mode
    PTLF("Deinitializing BLE device...");
    BLEDevice::deinit(false);  // Deinitialize BLE device, but keep memory
    delay(500);                // Wait for deinitialization to complete
  }
  if (activeBtMode != BT_MODE_CLIENT)
#endif
#if defined(BT_BLE)
  // Only start BLE server
  {
    activeBtMode = BT_MODE_SERVER;
    bleSetup();
    PTLF("BLE Server mode activated");
  }
#endif

#ifdef BT_SSP
  blueSspSetup();
#endif
}

void shutdownBleServer() {
#ifdef BT_BLE
  if (pServer) {
    pServer->getAdvertising()->stop();
    PTLF("BLE Server advertising stopped");
  }
  deviceConnected = false;
#endif
}

void shutdownBleClient() {
#ifdef BT_CLIENT
  PetoiBtStopScan();
#endif
}
