/* Write and read parameters to the permanent memory.
   Maximum bytes of I2C EEPROM is 65536 bit. i.e. address stops at 65535.
   Extra data will wrap over to address 0

   use ESP32's flash to simulate the EEPROM.

   Rongzhong Li
   September 2024

   Copyright (c) 2024 Petoi LLC.

  The MIT license

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <Preferences.h>
Preferences config;

int dataLen(int8_t p) {
  byte skillHeader = p > 0 ? 4 : 7;
  int frameSize = p > 1 ? WALKING_DOF :  // gait
                      p == 1 ? DOF
                             :  // posture
                      DOF + 4;  // behavior
  int len = skillHeader + abs(p) * frameSize;
  return len;
}

void i2cDetect(TwoWire &wirePort) {
  if (&wirePort == &Wire1)
    wirePort.begin(UART_TX2, UART_RX2, 400000);
  byte error, address;
  int nDevices;
  int8_t i2cAddress[] = {0x39, 0x50, 0x54, 0x60, 0x62, 0x68, 0x69};
  String i2cAddressName[] = {"APDS9960 Gesture", "Mu3 CameraP", "EEPROM",
                             "Mu3 Camera",
                             "AI Vision",        "MPU6050",     "ICM42670"};
  Serial.println("Scanning I2C network...");
  nDevices = 0;
  for (address = 1; address < 127; address++) {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    wirePort.beginTransmission(address);
    error = wirePort.endTransmission();
    if (error == 0) {
      Serial.print("- I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.print(":\t");
      for (byte i = 0; i < sizeof(i2cAddress) / sizeof(int8_t); i++) {
        if (address == i2cAddress[i]) {
          PT(i2cAddressName[i]);
          if (i == 1)
            MuQ = true;
          else if (i == 2)
            eepromQ = true;
          else if (i == 3)
            MuQ = true;  // The older Mu3 Camera and Sentry1 share the same address. Sentry is not supported yet.
          else if (i == 4)
            GroveVisionQ = true;

          else if (i == 5)
            mpuQ = true;

          else if (i == 6)
            icmQ = true;

          nDevices++;
          break;
        }
        if (i == sizeof(i2cAddress) / sizeof(int8_t) - 1) {
          PT("Misc.");
        }
      }
      PTL();
    } else if (error == 4) {
      Serial.print("- Unknown error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (!icmQ && !mpuQ) {
    updateGyroQ = false;
    PTL("No IMU detected!");
  }
  if (nDevices == 0)
    Serial.println("- No I2C devices found");
  else
    Serial.println("- done");
  if (&wirePort == &Wire1)
    wirePort.end();
  PTHL("GroveVisionQ", GroveVisionQ);
  PTHL("MuQ", MuQ);
}

bool newBoardQ() {
  return config.getChar("birthmark") != BIRTHMARK;
}

void resetAsNewBoard(char mark) {
  config.putChar("birthmark", mark);
  PTL("Alter the birthmark for reset!");
  delay(5);
  ESP.restart();
}

void genBleID(int suffixDigits = 2) {
  const char *prefix ="Bittle" ;

  int prelen = strlen(prefix);

  char *id = new char[prelen + suffixDigits + 1];
  strcpy(id, prefix);
  for (int i = 0; i < suffixDigits; i++) {
    int temp = esp_random() % 16;
    sprintf(id + prelen + i, "%X", temp);
  }
  id[prelen + suffixDigits] = '\0';

  config.putString("ID", id);
  uniqueName = String(id);

  PTL(uniqueName);
  delete[] id;
}

void customBleID(char *customName, int8_t len) {
  config.putString("ID", customName);

}

// Get device name with specified suffix using global uniqueName
// Returns a dynamically allocated string that must be freed by caller
char *getDeviceName(const char *suffix) {
  String deviceName = uniqueName + suffix;
  char *result = new char[deviceName.length() + 1];
  strcpy(result, deviceName.c_str());
  return result;
}

void resetIfVersionOlderThan(String versionStr) {
  String savedVersionStr = config.getString("versionDate", "unknown");
  long savedDate = (savedVersionStr == "unknown") ? 0 : savedVersionStr.substring(savedVersionStr.length() - 6).toInt();
  long currentDate = atol(versionStr.c_str() + versionStr.length() - 6);
  if (savedDate < currentDate) {
    delay(1000);
    PTTL("\n* The previous version on the board is ", savedVersionStr);
    PTTL("* The robot will reboot and upgrade to ", versionStr);
    resetAsNewBoard('X');
  }
}

void configSetup() {
  if (newBoard) {
    PTLF("Set up the new board...");
    char tempStr[12];
    strcpy(tempStr, SoftwareVersion.c_str());
    soundState = 1;
    buzzerVolume = 5;
    PTLF("Unmute and set volume to 5/10");

    int bufferLen = dataLen(rest[0]);  // save a preset skill to the temp skill
    arrayNCPY(newCmd, rest, bufferLen);
    PTF("- Name the new robot as: ");
#ifdef BT_BLE
    genBleID();
#endif

    PTL("Using constants from on-board Flash");
    config.putString("versionDate", tempStr);
    config.putBool("bootSndState", soundState);
    config.putChar("buzzerVolume", buzzerVolume);
    config.putBytes("moduleState", moduleActivatedQ, sizeof(moduleList) / sizeof(char));
    config.putChar("defaultLan", 'a');  // a for English, b for Chinese
    config.putChar("currentLan", 'b');  // a for English, b for Chinese
    // save a preset skill to the temp skill in case its called before assignment
    config.putInt("tmpLen", bufferLen);
    config.putBytes("tmp", (int8_t *)newCmd, bufferLen);
    config.putBool("WifiManager", rebootForWifiManagerQ);  // default is false

    // playMelody(melodyInit, sizeof(melodyInit) / 2);
    PTL("- Reset the joints' calibration offsets? (Y/n): ");
    char choice = getUserInputChar();
    PTL(choice);
    if (choice == 'Y' || choice == 'y') {
    config.putBytes("calib", servoCalib, DOF);
    }

  } else {
    resetIfVersionOlderThan(SoftwareVersion);

    soundState = config.getBool("bootSndState") ? 1 : 0;
    buzzerVolume = max(byte(0), min(byte(10), (byte)config.getChar("buzzerVolume")));
    config.getBytes("moduleState", moduleActivatedQ, sizeof(moduleList) / sizeof(char));
    defaultLan = config.getChar("defaultLan");
    currentLan = config.getChar("currentLan");
    uniqueName = config.getString("ID", "P");
    rebootForWifiManagerQ = config.getBool("WifiManager");
    PT(config.freeEntries());                                 // show remaining entries of the preferences.
    PTL(" entries are available in the namespace table.\n");  // this method works regardless of the mode in which the
                                                              // namespace is opened.
    PTHL("Default language: ", defaultLan == 'b' ? " Chinese" : " English");
      // playMelody(melodyNormalBoot, sizeof(melodyNormalBoot) / 2);
  }
}

void saveCalib(int8_t *var) {
  config.putBytes("calib", var, DOF);
  for (byte s = 0; s < DOF; s++) {
    calibratedZeroPosition[s] = zeroPosition[s] + float(var[s]) * rotationDirection[s];
  }
}

// clang-format off
// Forward Declarations
bool listEspPartitions();
bool listUniqueNvsNamespaces();
bool listNamespacesWithKeysAndValues(const char *partition_label);
bool listKeysAndValues(const char *partition_label, const char *namespace_name);

void displayNsvPartition()
{
/*  Created by este este - 28-MAR-2025
      @ Lists all partitions on the ESP32 chip.
      @ If the default 'nsv' partition is found then any namespaces in that partition are listed.
      @ For each namespace in the default 'nsv' partition, the key-value pairs are listed.
*/

/*  The ESP32 Non-Volatile Storage (NVS) system supports the following value types for storing data via the nvs_type_t enum which has the following values:
    NVS_TYPE_U8: Unsigned 8-bit integer.
    NVS_TYPE_I8: Signed 8-bit integer.
    NVS_TYPE_U16: Unsigned 16-bit integer.
    NVS_TYPE_I16: Signed 16-bit integer.
    NVS_TYPE_U32: Unsigned 32-bit integer.
    NVS_TYPE_I32: Signed 32-bit integer.
    NVS_TYPE_U64: Unsigned 64-bit integer.
    NVS_TYPE_I64: Signed 64-bit integer.
    NVS_TYPE_STR: Null-terminated string.
    NVS_TYPE_BLOB: Binary large object (arbitrary binary data).
    NVS_TYPE_ANY: A special type used during iteration to indicate that any type of entry is acceptable.
*/

  if ( !listEspPartitions() )
    {
      return;
    }

  if ( !listUniqueNvsNamespaces() )
    {
      return;
    }

  if ( !listNamespacesWithKeysAndValues("nvs") )
    {
      return;
    }
}

bool listEspPartitions()
{
  // Created by este este

  /* <<<<< FIND ALL PARTITIONS >>>>> */

  bool defaultNvsPartitionFoundQ = false;

  // Tag for logging
  const char *TAG __attribute__((unused)) = "PARTITIONS";

  // Iterator to find all partitions
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);

  if ( it == NULL )
    {
      ESP_LOGI(TAG, "No partitions found");
      Serial.println("No partitions found.");
      return defaultNvsPartitionFoundQ;
    }

  Serial.println("\nLocating ALL Partitions...\n");

  // Iterate through all partitions
  while ( it != NULL )
    {
      const esp_partition_t *partition = esp_partition_get(it);

      // Print partition details
      ESP_LOGI(TAG, "Found Partition:");
      ESP_LOGI(TAG, "Label: %s, Address: 0x%X, Size: %d bytes, Type: %d, Subtype: %d", partition->label, partition->address, partition->size, partition->type, partition->subtype);

      Serial.printf("Found Partition:\n");
      Serial.printf("\tLabel: %s\n", partition->label);

      if ( partition->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS )
        {
          Serial.printf("     Partition labeled '%s' is an NVS partition.\n", partition->label);
          if ( String(partition->label) == "nvs" )
            {
              defaultNvsPartitionFoundQ = true;
            }
        }

      Serial.printf("\tAddress: 0x%X\n", partition->address);
      Serial.printf("\tSize: %d bytes\n", partition->size);
      Serial.printf("\tType: %d\n", partition->type);
      Serial.printf("\tSubtype: %d\n", partition->subtype);
      Serial.println(); // Add a blank line for readability

      // Move to the next partition
      it = esp_partition_next(it);
    }

  // Release the iterator
  esp_partition_iterator_release(it);

  if ( !defaultNvsPartitionFoundQ )
    {
      Serial.printf("\nDefault 'nvs' partition was NOT found so exiting.\n");
      return defaultNvsPartitionFoundQ;
    }
  else
    {
      defaultNvsPartitionFoundQ = true;
      Serial.printf("\nDefault 'nvs' partition WAS found so continuing.\n");
      return defaultNvsPartitionFoundQ;
    }
}

bool listUniqueNvsNamespaces()
{
  // Created by este este

  bool defaultNvsNameSpaceFoundQ = false;

  // Initialize default NVS partition 'nvs'
  esp_err_t err = nvs_flash_init();  // only works for the default partition.
  if ( err != ESP_OK )
    {
      Serial.printf("Failed to initialize NVS partition with default name of 'nvs': %s\n", esp_err_to_name(err));
      return defaultNvsNameSpaceFoundQ;
    }

  // Create an iterator for entries in NVS
  nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY);
  if ( it == NULL )
    {
      Serial.println("\nNo namespaces found.");
      return defaultNvsNameSpaceFoundQ;
    }

  std::set<String> uniqueNamespaces;  // To store unique namespace names
  Serial.println("\nNamespaces in the default 'nvs' partition:");
  while ( it != NULL )
    {
      defaultNvsNameSpaceFoundQ = true;
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);

      // Add unique namespaces to the set
      if ( uniqueNamespaces.find(String(info.namespace_name)) == uniqueNamespaces.end() )
        {
          uniqueNamespaces.insert(String(info.namespace_name));
          Serial.printf("- Namespace: %s\n", info.namespace_name);
        }

      it = nvs_entry_next(it);
    }
  return defaultNvsNameSpaceFoundQ;
}

bool listNamespacesWithKeysAndValues(const char *partition_label)
{
  // Created by este este

  bool             successQ = false;

  std::set<String> uniqueNamespaces;  // Store unique namespace names
  nvs_iterator_t   it = nvs_entry_find(partition_label, NULL, NVS_TYPE_ANY);
  if ( it == NULL )
    {
      Serial.printf("\nNo namespaces found in partition '%s'\n", partition_label);
      return successQ;
    }

  while ( it != NULL )
    {
      successQ = true;
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);

      // Add to unique set and process if not already listed
      if ( uniqueNamespaces.find(String(info.namespace_name)) == uniqueNamespaces.end() )
        {
          uniqueNamespaces.insert(String(info.namespace_name));
          Serial.printf("\nNamespace: %s\n", info.namespace_name);
          listKeysAndValues(partition_label, info.namespace_name);
        }
      it = nvs_entry_next(it);
    }
  return successQ;
}

bool listKeysAndValues(const char *partition_label, const char *namespace_name)
{
  // Created by este este

  bool         successQ = false;
  nvs_handle_t handle;
  esp_err_t    err;

  // Open the namespace in the specified partition
  err = nvs_open_from_partition(partition_label, namespace_name, NVS_READONLY, &handle);
  if ( err != ESP_OK )
    {
      Serial.printf("\nFailed to open namespace '%s' in partition '%s'\n", namespace_name, partition_label);
      return successQ;
    }

  Serial.printf("Keys and values in namespace '%s':\n", namespace_name);

  nvs_iterator_t it = nvs_entry_find(partition_label, namespace_name, NVS_TYPE_ANY);
  while ( it != NULL )
    {
      successQ = true;
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);

      Serial.printf("- Key: %s\n", info.key);

      // Handle all supported value types
      switch ( info.type )
        {
          case NVS_TYPE_U8:
            {
              uint8_t value;
              if ( nvs_get_u8(handle, info.key, &value) == ESP_OK )
                {
                  Serial.printf("  Value (uint8): %u\n", value);
                }
              break;
            }
          case NVS_TYPE_I8:
            {
              int8_t value;
              if ( nvs_get_i8(handle, info.key, &value) == ESP_OK )
                {
                  Serial.printf("  Value (int8): %d\n", value);
                }
              break;
            }
          case NVS_TYPE_U16:
            {
              uint16_t value;
              if ( nvs_get_u16(handle, info.key, &value) == ESP_OK )
                {
                  Serial.printf("  Value (uint16): %u\n", value);
                }
              break;
            }
          case NVS_TYPE_I16:
            {
              int16_t value;
              if ( nvs_get_i16(handle, info.key, &value) == ESP_OK )
                {
                  Serial.printf("  Value (int16): %d\n", value);
                }
              break;
            }
          case NVS_TYPE_U32:
            {
              uint32_t value;
              if ( nvs_get_u32(handle, info.key, &value) == ESP_OK )
                {
                  Serial.printf("  Value (uint32): %u\n", value);
                }
              break;
            }
          case NVS_TYPE_I32:
            {
              int32_t value;
              if ( nvs_get_i32(handle, info.key, &value) == ESP_OK )
                {
                  Serial.printf("  Value (int32): %d\n", value);
                }
              break;
            }
          case NVS_TYPE_U64:
            {
              uint64_t value;
              if ( nvs_get_u64(handle, info.key, &value) == ESP_OK )
                {
                  Serial.printf("  Value (uint64): %llu\n", value);
                }
              break;
            }
          case NVS_TYPE_I64:
            {
              int64_t value;
              if ( nvs_get_i64(handle, info.key, &value) == ESP_OK )
                {
                  Serial.printf("  Value (int64): %lld\n", value);
                }
              break;
            }
          case NVS_TYPE_STR:
            {
              size_t required_size = 0;
              nvs_get_str(handle, info.key, NULL, &required_size);
              char *value = (char *)malloc(required_size);
              if ( value != NULL && nvs_get_str(handle, info.key, value, &required_size) == ESP_OK )
                {
                  Serial.printf("  Value (string): %s\n", value);
                }
              free(value);
              break;
            }
          case NVS_TYPE_BLOB:
            {
              size_t required_size = 0;
              nvs_get_blob(handle, info.key, NULL, &required_size);
              uint8_t *blob = (uint8_t *)malloc(required_size);
              if ( blob != NULL && nvs_get_blob(handle, info.key, blob, &required_size) == ESP_OK )
                {
                  Serial.printf("  Value (blob): [size: %d bytes]\n", required_size);
                }
              free(blob);
              break;
            }
          default:
            Serial.printf("  Unsupported type\n");
            break;
        }
      it = nvs_entry_next(it);
    }
  nvs_close(handle);
  return successQ;
}
