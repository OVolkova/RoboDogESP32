#include <WebSocketsServer.h>  // download at https://github.com/Links2004/arduinoWebSockets/
#include <WiFi.h>
#include "esp32-hal.h"

#include <esp_wifi.h>


#include <ArduinoJson.h>
#include <map>

// Web server debug level control
#define WEB_DEBUG_LEVEL 1  // 0=off, 1=error, 2=warning, 3=info, 4=verbose

// Debug print macros - controlled by level
#if WEB_DEBUG_LEVEL >= 1
#define WEB_ERROR(msg, value) PTHL(msg, value)
#define WEB_ERROR_F(msg) PTLF(msg)
#else
#define WEB_ERROR(msg, value)
#define WEB_ERROR_F(msg)
#endif

#if WEB_DEBUG_LEVEL >= 2
#define WEB_WARN(msg, value) PTHL(msg, value)
#define WEB_WARN_F(msg) PTLF(msg)
#else
#define WEB_WARN(msg, value)
#define WEB_WARN_F(msg)
#endif

#if WEB_DEBUG_LEVEL >= 3
#define WEB_INFO(msg, value) PTHL(msg, value)
#define WEB_INFO_F(msg) PTLF(msg)
#else
#define WEB_INFO(msg, value)
#define WEB_INFO_F(msg)
#endif

#if WEB_DEBUG_LEVEL >= 4
#define WEB_DEBUG(msg, value) PTHL(msg, value)
#define WEB_DEBUG_F(msg) PTLF(msg)
#else
#define WEB_DEBUG(msg, value)
#define WEB_DEBUG_F(msg)
#endif

// Web server timeout configuration (milliseconds) - optimized for Bluetooth coexistence
#define HEARTBEAT_TIMEOUT 40000           // Heartbeat timeout: 40 seconds (increased buffer time for BLE interference)
#define HEALTH_CHECK_INTERVAL 15000       // Health check interval: 15 seconds (reduced check frequency)
#define WEB_TASK_EXECUTION_TIMEOUT 45000  // Task execution timeout: 45 seconds (increased execution time)
#define MAX_CLIENTS 2                     // Maximum client connections limit

// WiFi configuration
String ssid = "";
String password = "";
WebSocketsServer webSocket = WebSocketsServer(81);  // WebSocket server on port 81
long connectWebTime;
bool webServerConnected = false;

// WebSocket client management
std::map<uint8_t, bool> connectedClients;
std::map<uint8_t, unsigned long> lastHeartbeat;  // Record last heartbeat time for each client

// Connection health check
unsigned long lastHealthCheckTime = 0;

// Asynchronous task management
struct WebTask {
  String taskId;
  String status;  // "pending", "running", "completed", "error"
  unsigned long timestamp;
  unsigned long endTime;
  unsigned long startTime;
  bool resultReady;
  uint8_t clientId;                  // Add client ID
  std::vector<String> commandGroup;  // List of commands in the command group
  std::vector<String> results;       // Execution results in the command group
  size_t currentCommandIndex;        // Current executing command index
};

std::map<String, WebTask> webTasks;
String currentWebTaskId = "";
bool webTaskActive = false;

// Function declarations
String generateTaskId();
void startWebTask(String taskId);
void completeWebTask();
void errorWebTask(String errorMessage);
void processNextWebTask();
void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
void clearWebTask(String taskId);
void checkConnectionHealth();
void sendSocketResponse(uint8_t clientId, String message);

// Simple Base64 decode function
String base64Decode(String input) {
  const char PROGMEM b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String result = "";
  int val = 0, valb = -8;

  for (char c : input) {
    if (c == '=') break;

    int index = -1;
    for (int i = 0; i < 64; i++) {
      if (pgm_read_byte(&b64_alphabet[i]) == c) {
        index = i;
        break;
      }
    }

    if (index == -1) continue;

    val = (val << 6) | index;
    valb += 6;

    if (valb >= 0) {
      result += char((val >> valb) & 0xFF);
      valb -= 8;
    }
  }

  return result;
}

// Generate task ID
String generateTaskId() {
  return String(millis()) + "_" + String(esp_random() % 1000);
}

// Send response to specified client
void sendSocketResponse(uint8_t clientId, String message) {
  if (connectedClients.find(clientId) != connectedClients.end() && connectedClients[clientId]) {
    webSocket.sendTXT(clientId, message);
  }
}

// Check connection health status
void checkConnectionHealth() {
  unsigned long currentTime = millis();

  // Check if there is BLE activity, if so, relax heartbeat timeout
  bool bleActive = false;
#ifdef BT_CLIENT
  extern boolean doScan;
  extern boolean btConnected;
  bleActive = doScan || btConnected;
#endif

  unsigned long effectiveTimeout = bleActive ? (HEARTBEAT_TIMEOUT + 15000) : HEARTBEAT_TIMEOUT;

  // Check heartbeat timeout
  for (auto it = lastHeartbeat.begin(); it != lastHeartbeat.end();) {
    uint8_t clientId = it->first;
    unsigned long lastHeartbeatTime = it->second;

    if (currentTime - lastHeartbeatTime > effectiveTimeout) {
      if (bleActive) {
        WEB_WARN("Client heartbeat timeout during BLE activity: ", clientId);
      } else {
        WEB_ERROR("Client heartbeat timeout, disconnecting: ", clientId);
      }

      // Send timeout notification (including BLE status information)
      String timeoutMsg = bleActive ? "{\"type\":\"error\",\"error\":\"Heartbeat timeout during BLE scan\"}"
                                    : "{\"type\":\"error\",\"error\":\"Heartbeat timeout\"}";
      sendSocketResponse(clientId, timeoutMsg);

      // Disconnect connection
      webSocket.disconnect(clientId);

      // Clean up client state
      connectedClients.erase(clientId);
      it = lastHeartbeat.erase(it);

      // If current task belongs to this client, needs handling
      if (webTaskActive && currentWebTaskId != "" && webTasks.find(currentWebTaskId) != webTasks.end() &&
          webTasks[currentWebTaskId].clientId == clientId) {
        errorWebTask("Client disconnected due to heartbeat timeout");
      }
    } else {
      ++it;
    }
  }
}

// WebSocket event handling
void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      WEB_ERROR("WebSocket client disconnected: ", num);

      // Clean up client state
      connectedClients.erase(num);
      lastHeartbeat.erase(num);

      // If current task belongs to this client, needs handling
      if (webTaskActive && currentWebTaskId != "" && webTasks.find(currentWebTaskId) != webTasks.end() &&
          webTasks[currentWebTaskId].clientId == num) {
        errorWebTask("Client disconnected");
      }
      break;

    case WStype_CONNECTED:
      // Check connection limit
      if (connectedClients.size() >= MAX_CLIENTS) {
        WEB_ERROR("Max clients reached, rejecting: ", num);
        sendSocketResponse(num, "{\"type\":\"error\",\"error\":\"Max clients reached\"}");
        webSocket.disconnect(num);
        return;
      }

      connectedClients[num] = true;
      lastHeartbeat[num] = millis();
      WEB_DEBUG("WebSocket client connected: ", num);

      // Send connection success response
      sendSocketResponse(num, "{\"type\":\"connected\",\"clientId\":\"" + String(num) + "\"}");
      break;

    case WStype_TEXT: {
      String message = String((char*)payload);

      // Parse JSON message
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, message);

      if (error) {
        // JSON parsing error, send error response
        sendSocketResponse(num, "{\"type\":\"error\",\"error\":\"Invalid JSON format\"}");
        return;
      }

      String msgType = doc["type"].as<String>();
      WEB_DEBUG("msg type: ", msgType);

      // Handle heartbeat message
      if (doc["type"] == "heartbeat") {
        lastHeartbeat[num] = millis();
        sendSocketResponse(num, "{\"type\":\"heartbeat\",\"timestamp\":" + String(millis()) + "}");
        return;
      }

      // Handle command message (uniformly use command group format)
      if (doc["type"] == "command") {
        String taskId = doc["taskId"].as<String>();
        JsonArray commands;

        // If single command, convert to command group format
        commands = doc["commands"].as<JsonArray>();

        // Update heartbeat time
        lastHeartbeat[num] = millis();

        // Create task record
        WebTask task;
        task.taskId = taskId;
        task.status = "pending";
        task.timestamp = millis();
        task.startTime = 0;
        task.resultReady = false;
        task.clientId = num;
        task.currentCommandIndex = 0;

        // Store command group
        for (JsonVariant cmd : commands) { task.commandGroup.push_back(cmd.as<String>()); }

        // Debug information
        WEB_DEBUG("Received command task: ", taskId);
        WEB_DEBUG("Command count: ", task.commandGroup.size());
#if WEB_DEBUG_LEVEL >= 4
        for (size_t i = 0; i < task.commandGroup.size(); i++) {
          WEB_DEBUG("Command " + String(i) + ": ", task.commandGroup[i]);
        }
#endif

        // If there is no active web task currently, start execution immediately
        if (!webTaskActive) {
          // Store task
          webTasks[taskId] = task;
          startWebTask(taskId);
        } else {
          // If there is an active web task currently, discard and return error
          errorWebTask("Previous web task is still running");
          return;
        }

        // Send task start response
        sendSocketResponse(num, "{\"type\":\"response\",\"taskId\":\"" + taskId + "\",\"status\":\"running\"}");

        WEB_DEBUG("web command group async: ", taskId);
        WEB_DEBUG("command count: ", task.commandGroup.size());
      }
      break;
    }
  }
}

// Start executing web task
void startWebTask(String taskId) {
  if (webTasks.find(taskId) == webTasks.end()) { return; }

  WebTask& task = webTasks[taskId];

  // Set global flags and commands
  cmdFromWeb = true;
  currentWebTaskId = taskId;
  webTaskActive = true;
  webResponse = "";  // Clear response buffer

  // Execute next command in command group
  if (task.currentCommandIndex < task.commandGroup.size()) {
    String webCmd = task.commandGroup[task.currentCommandIndex];

    WEB_DEBUG("Processing command: ", webCmd);

    // Check if it's a base64 encoded command
    if (webCmd.startsWith("b64:")) {
      String base64Cmd = webCmd.substring(4);
      String decodedString = base64Decode(base64Cmd);
      if (decodedString.length() > 0) {
        token = decodedString[0];
        for (int i = 1; i < decodedString.length(); i++) {
          int8_t param = (int8_t)decodedString[i];
          newCmd[i - 1] = param;
        }
        // strcpy(newCmd, decodedString.c_str() + 1);
        cmdLen = decodedString.length() - 1;
        if (token >= 'A' && token <= 'Z') {
          newCmd[cmdLen] = '~';
        } else {
          newCmd[cmdLen] = '\0';
        }
        WEB_DEBUG("base64 decode token: ", token);
        WEB_DEBUG("base64 decode args count: ", cmdLen);
      } else {
        WEB_ERROR("base64 decode failed: ", task.currentCommandIndex);
        // Base64 decode failed, skip this command
        task.currentCommandIndex++;
        startWebTask(taskId);
        return;
      }
    } else {
      // Parse command
      token = webCmd[0];
      strcpy(newCmd, webCmd.c_str() + 1);
      cmdLen = strlen(newCmd);
      newCmd[cmdLen + 1] = '\0';

      WEB_DEBUG("Parsed token: ", token);
      WEB_DEBUG("Parsed command: ", newCmd);
      WEB_DEBUG("Command length: ", cmdLen);
    }
    newCmdIdx = 4;

    // Update task status
    task.status = "running";
    task.startTime = millis();

    // Notify client that task has started
    JsonDocument statusDoc;
    statusDoc["type"] = "response";
    statusDoc["taskId"] = taskId;
    statusDoc["status"] = "running";
    String statusMsg;
    serializeJson(statusDoc, statusMsg);
    webSocket.sendTXT(task.clientId, statusMsg);

    WEB_DEBUG("executing command group task: ", taskId);
    WEB_DEBUG("sub command Index: ", task.currentCommandIndex);
    WEB_DEBUG("sub command: ", webCmd);
    WEB_DEBUG("total commands: ", task.commandGroup.size());
  } else {
    // All commands executed
    completeWebTask();
  }
}

// Complete web task
void completeWebTask() {
  if (!webTaskActive || currentWebTaskId == "") { return; }

  if (webTasks.find(currentWebTaskId) != webTasks.end()) {
    WebTask& task = webTasks[currentWebTaskId];
    task.results.push_back(webResponse);

    // Check if there is a next command
    if (task.currentCommandIndex + 1 < task.commandGroup.size()) {
      // There is a next command, continue execution
      task.currentCommandIndex++;
      startWebTask(currentWebTaskId);
      return;
    }

    // All commands executed
    task.status = "completed";
    task.endTime = millis();
    task.resultReady = true;

    WEB_DEBUG("web task completed: ", currentWebTaskId);
    WEB_DEBUG("results length: ", task.results.size());

    // Send completion status to client
    JsonDocument completeDoc;
    completeDoc["type"] = "response";
    completeDoc["taskId"] = currentWebTaskId;
    completeDoc["status"] = "completed";
    JsonArray results = completeDoc["results"].to<JsonArray>();
    for (String result : task.results) { results.add(result); }
    String statusMsg;
    serializeJson(completeDoc, statusMsg);
    sendSocketResponse(task.clientId, statusMsg);
    WEB_DEBUG("web task response: ", statusMsg);
    clearWebTask(currentWebTaskId);
  }

  // Reset global state
  cmdFromWeb = false;
  webTaskActive = false;
  currentWebTaskId = "";

  // Check if there are waiting tasks
  processNextWebTask();
}

// Web task error handling
void errorWebTask(String errorMessage) {
  if (!webTaskActive || currentWebTaskId == "") { return; }

  if (webTasks.find(currentWebTaskId) != webTasks.end()) {
    WebTask& task = webTasks[currentWebTaskId];
    task.status = "error";
    task.resultReady = true;

    // Send error status to client
    JsonDocument errorDoc;
    errorDoc["type"] = "response";
    errorDoc["taskId"] = currentWebTaskId;
    errorDoc["status"] = "error";
    errorDoc["error"] = errorMessage;
    String statusMsg;
    serializeJson(errorDoc, statusMsg);
    sendSocketResponse(task.clientId, statusMsg);
    clearWebTask(currentWebTaskId);
  }

  // Reset state
  cmdFromWeb = false;
  webTaskActive = false;
  currentWebTaskId = "";

  // Process next task
  processNextWebTask();
}

void clearWebTask(String taskId) {
  if (webTasks.find(taskId) != webTasks.end()) {
    WebTask& task = webTasks[taskId];
    WEB_DEBUG("clear web task: ", taskId);
    task.commandGroup.clear();
    task.results.clear();
    webTasks.erase(taskId);
  }
}

// Process next waiting task
void processNextWebTask() {
  for (auto& pair : webTasks) {
    WebTask& task = pair.second;
    if (task.status == "pending") {
      startWebTask(task.taskId);
      break;
    }
  }
}

// WiFi configuration function - enhanced version, supports retry mechanism
bool connectWifi(String ssid, String password, int maxRetries = 3) {
  for (int retry = 0; retry < maxRetries; retry++) {
    if (retry > 0) {
      WEB_WARN("WiFi connection retry: ", retry);
      delay(2000);  // Wait 2 seconds before retry
    }

    WiFi.begin(ssid.c_str(), password.c_str());
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 100) {
      delay(100);
#if WEB_DEBUG_LEVEL >= 3
      PT('.');
#endif
      timeout++;
    }
#if WEB_DEBUG_LEVEL >= 3
    PTL();
#endif

    if (WiFi.status() == WL_CONNECTED) {
      WEB_INFO("WiFi connected on attempt: ", retry + 1);
      return true;
    } else {
      WEB_ERROR("WiFi connection failed on attempt: ", retry + 1);
      WiFi.disconnect(true);  // Completely disconnect, prepare for next attempt
    }
  }

  Serial.println("All WiFi connection attempts failed");
  return false;
}

// When WIFI_MANAGER is not enabled, attempt to read and use previously saved WiFi info to connect
bool connectWifiFromStoredConfig() {
  // Check available memory
  size_t freeHeap = ESP.getFreeHeap();
  WEB_INFO("Free heap before WiFi init: ", freeHeap);

  if (freeHeap < 50000) {  // If available memory is less than 50KB
    WEB_ERROR("Insufficient memory for WiFi initialization: ", freeHeap);
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  wifi_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) {
    WEB_ERROR_F("Failed to get stored WiFi config");
    return false;
  }

  String savedSsid = String(reinterpret_cast<char*>(cfg.sta.ssid));
  String savedPassword = String(reinterpret_cast<char*>(cfg.sta.password));

  if (savedSsid.length() == 0) {
    WEB_WARN_F("No stored SSID found");
    return false;
  }

  webServerConnected = connectWifi(savedSsid, savedPassword);

  if (webServerConnected) {
    printToAllPorts("Successfully connected Wifi to IP Address: " + WiFi.localIP().toString());
    // Start WebSocket server
    webSocket.begin();
    webSocket.onEvent(handleWebSocketEvent);
    WEB_INFO_F("WebSocket server started");

    // Display memory state after connection
    size_t freeHeapAfter = ESP.getFreeHeap();
    WEB_INFO("Free heap after WiFi connection: ", freeHeapAfter);
  } else {
    WEB_ERROR_F("Timeout: Fail to connect web server!");
  }
  return webServerConnected;
}

void resetWifiManager() {
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  delay(2000);
  if (esp_wifi_restore() != ESP_OK) {
    WEB_ERROR_F("\nWiFi is not initialized by esp_wifi_init ");
  } else {
    WEB_INFO_F("\nWiFi Configurations Cleared!");
  }
  delay(2000);
  ESP.restart();
}

// Main loop function
void WebServerLoop() {
  if (webServerConnected) {
    webSocket.loop();

    // Monitor BLE activity's impact on WebSocket
    static unsigned long lastBleStatusLog = 0;
    unsigned long currentTime = millis();

    if (currentTime - lastBleStatusLog > 30000) {  // Log status every 30 seconds
#ifdef BT_CLIENT
      extern boolean doScan;
      extern boolean btConnected;
      if (doScan || btConnected) {
        WEB_INFO("BLE active - doScan: ", doScan);
        WEB_INFO("BLE connected: ", btConnected);
        WEB_INFO("Active WebSocket clients: ", connectedClients.size());
      }
#endif
      lastBleStatusLog = currentTime;
    }

    // Regularly check connection health status
    if (currentTime - lastHealthCheckTime > HEALTH_CHECK_INTERVAL) {
      checkConnectionHealth();
      lastHealthCheckTime = currentTime;
    }

    // Check task timeout
    for (auto& pair : webTasks) {
      WebTask& task = pair.second;
      if (task.status == "running" && task.startTime > 0) {
        if (currentTime - task.startTime > WEB_TASK_EXECUTION_TIMEOUT) {  // Use configured task execution timeout
          WEB_ERROR("web task timeout: ", task.taskId);
          task.status = "error";
          task.resultReady = true;

          // Send timeout status to client
          sendSocketResponse(task.clientId,
                             "{\"taskId\":\"" + task.taskId + "\",\"status\":\"error\",\"error\":\"Task timeout\"}");

          if (task.taskId == currentWebTaskId) {
            cmdFromWeb = false;
            webTaskActive = false;
            currentWebTaskId = "";
            processNextWebTask();
          }
        }
      }
    }
  }
}
