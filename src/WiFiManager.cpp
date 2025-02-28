// WiFiManager.cpp
#include "WiFiManager.h"
#include "Utils.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

// WiFi settings
const char* AP_SSID = "ES32A08-Setup"; // Default AP SSID
const char* AP_PASSWORD = "password";  // Default AP password

// Global variables
WiFiConfig wifiStationConfig;
volatile bool wifiTestInProgress = false;
bool stationConnected = false;

void initWiFiManager() {
  debugPrintln("DEBUG: Initializing WiFi manager...");
  
  // Load WiFi configuration first
  loadWiFiConfig();
  
  // Set WiFi event handler
  WiFi.onEvent(WiFiEventHandler);
  
  // Start with AP mode only to ensure it's available immediately
  WiFi.mode(WIFI_AP);
  
  // Configure and start AP
  if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    debugPrintln("DEBUG: AP Mode initialized successfully");
  } else {
    debugPrintln("DEBUG: Failed to initialize AP Mode!");
  }
  
  // Short delay to ensure AP is fully started
  delay(100);
  
  IPAddress apIP = WiFi.softAPIP();
  debugPrintln("DEBUG: AP Mode IP Address: ");
  debugPrintln(apIP.toString().c_str());
  
  // If STA mode is enabled, connect to WiFi network after AP is ready
  if (wifiStationConfig.enabled && strlen(wifiStationConfig.ssid) > 0) {
    debugPrintf("DEBUG: Setting up Station mode, connecting to: %s\n", wifiStationConfig.ssid);
    WiFi.mode(WIFI_AP_STA);  // Set dual mode
    WiFi.begin(wifiStationConfig.ssid, wifiStationConfig.password);
    // Connection will be handled asynchronously by the event handler
  }
  
  debugPrintln("DEBUG: WiFi manager initialized");
}

const char* getAPSSID() {
  return AP_SSID;
}

const char* getAPPassword() {
  return AP_PASSWORD;
}

void WiFiEventHandler(WiFiEvent_t event) {
  // Declare variables outside the switch statement to avoid jump errors
  struct tm timeinfo;
  char timeStr[64];
  int retry;
  
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      debugPrintln("DEBUG: WiFi connected! IP address: ");
      debugPrintln(WiFi.localIP().toString().c_str());
      stationConnected = true;
      
      // Now that we have Internet, sync time with NTP
      debugPrintln("DEBUG: WiFi connected, initializing NTP time sync...");
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      
      // Check if time was set successfully
      retry = 0;
      while (!getLocalTime(&timeinfo) && retry < 5) {
        debugPrintln("DEBUG: Waiting for NTP time sync...");
        delay(1000);
        retry++;
      }
      
      if (retry < 5) {
        strftime(timeStr, sizeof(timeStr), "%c", &timeinfo);
        debugPrintf("DEBUG: Time synchronized: %s\n", timeStr);
      } else {
        debugPrintln("DEBUG: NTP time sync timeout, will retry later");
      }
      break;
      
    case SYSTEM_EVENT_STA_DISCONNECTED:
      debugPrintln("DEBUG: WiFi lost connection");
      stationConnected = false;
      break;
      
    default:
      break;
  }
}

void setupDualWiFi() {
  // This function is now replaced by the sequence in initWiFiManager
  // It's kept for backward compatibility but just calls initWiFiManager
  initWiFiManager();
}

// The remaining functions are unchanged
// ...
void handleGetWiFiStatus(AsyncWebServerRequest *request) {
  debugPrintln("DEBUG: API request received: /api/wifi/status");
  
  DynamicJsonDocument doc(512);
  
  // AP mode info is always available
  doc["apSsid"] = getAPSSID();
  doc["apEnabled"] = true;
  doc["apIp"] = WiFi.softAPIP().toString();
  doc["apStations"] = WiFi.softAPgetStationNum();
  
  // STA mode info
  doc["staEnabled"] = wifiStationConfig.enabled;
  doc["staSsid"] = wifiStationConfig.ssid;
  
  // Don't send the actual password, just whether it's set
  doc["staPasswordSet"] = (strlen(wifiStationConfig.password) > 0);
  
  // Connection status
  doc["staConnected"] = (WiFi.status() == WL_CONNECTED);
  if (WiFi.status() == WL_CONNECTED) {
    doc["staIp"] = WiFi.localIP().toString();
    doc["staRssi"] = WiFi.RSSI();
  }
  
  // Time synchronization status
  time_t now;
  struct tm timeinfo;
  time(&now);
  bool timeIsValid = getLocalTime(&timeinfo);
  doc["timeSync"] = timeIsValid;
  
  if (timeIsValid) {
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    doc["currentTime"] = String(timeStr);
  }
  
  String response;
  serializeJson(doc, response);
  
  request->send(200, "application/json", response);
}

void handleSetWiFiCredentials(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("DEBUG: API request received: /api/wifi/config");
  
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  // Check for required parameters
  if (!doc.containsKey("ssid") || !doc.containsKey("password") || !doc.containsKey("enabled")) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameters\"}");
    return;
  }
  
  // Update WiFi configuration
  strlcpy(wifiStationConfig.ssid, doc["ssid"].as<const char*>(), sizeof(wifiStationConfig.ssid));
  strlcpy(wifiStationConfig.password, doc["password"].as<const char*>(), sizeof(wifiStationConfig.password));
  wifiStationConfig.enabled = doc["enabled"].as<bool>();
  
  // Save configuration
  saveWiFiConfig();
  
  // Apply settings if needed
  if (wifiStationConfig.enabled) {
    // Apply new settings immediately
    WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(wifiStationConfig.ssid, wifiStationConfig.password);
    debugPrintln("DEBUG: Applying new WiFi settings and connecting...");
  } else if (WiFi.getMode() == WIFI_AP_STA) {
    // Disable STA mode if it was enabled
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    debugPrintln("DEBUG: Disabling STA mode as requested");
  }
  
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"WiFi settings updated\"}");
}

void handleTestWiFiConnection(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  // If a test is already in progress, return busy status
  if (wifiTestInProgress) {
    request->send(200, "application/json", "{\"status\":\"error\",\"message\":\"Another test is in progress\"}");
    return;
  }

  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  // Extract parameters
  const char* ssid = doc.containsKey("ssid") ? doc["ssid"].as<const char*>() : "";
  const char* password = doc.containsKey("password") ? doc["password"].as<const char*>() : "";
  
  // Validate SSID
  if (strlen(ssid) == 0) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SSID is required\"}");
    return;
  }

  // Send immediate response that test has started
  request->send(200, "application/json", "{\"status\":\"pending\",\"message\":\"WiFi test started\"}");
  
  // Create a simple task to test WiFi
  xTaskCreatePinnedToCore(
    [](void* parameter) {
      wifiTestInProgress = true;
      
      // Copy parameters
      char ssidCopy[33]; // 32 chars + null terminator
      char passwordCopy[65]; // 64 chars + null terminator
      
      strncpy(ssidCopy, (const char*)parameter, 32);
      ssidCopy[32] = '\0'; // Ensure null termination
      
      // Get password from parameter (it's stored after the SSID)
      strncpy(passwordCopy, ((const char*)parameter) + strlen(ssidCopy) + 1, 64);
      passwordCopy[64] = '\0'; // Ensure null termination
      
      // Remember current WiFi status
      bool wasConnected = (WiFi.status() == WL_CONNECTED);
      char oldSsid[33] = {0};
      char oldPassword[65] = {0};
      
      if (wasConnected) {
        strncpy(oldSsid, wifiStationConfig.ssid, 32);
        strncpy(oldPassword, wifiStationConfig.password, 64);
      }
      
      // Start connection test
      debugPrintf("DEBUG: Testing WiFi connection to %s\n", ssidCopy);
      WiFi.disconnect();
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(ssidCopy, passwordCopy);
      
      // Wait for connection with timeout
      int timeout = 0;
      while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        timeout++;
      }
      
      // Log result
      if (WiFi.status() == WL_CONNECTED) {
        debugPrintf("DEBUG: Test connection successful! IP: %s\n", WiFi.localIP().toString().c_str());
      } else {
        debugPrintf("DEBUG: Test connection failed. Status: %d\n", WiFi.status());
      }
      
      // Restore previous connection
      WiFi.disconnect();
      if (wasConnected) {
        WiFi.begin(oldSsid, oldPassword);
      } else {
        WiFi.mode(WIFI_AP); // AP mode only
      }
      
      // Free parameter memory
      free(parameter);
      wifiTestInProgress = false;
      
      vTaskDelete(NULL);
    },
    "WiFiTest",
    8192,
    createWiFiTestParam(ssid, password), // Create a parameter block
    1,
    NULL,
    0
  );
}

void saveWiFiConfig() {
  File file = SPIFFS.open("/wifi_config.json", FILE_WRITE);
  if (!file) {
    debugPrintln("DEBUG: Failed to open WiFi config file for writing");
    return;
  }
  
  DynamicJsonDocument doc(256);
  doc["ssid"] = wifiStationConfig.ssid;
  doc["password"] = wifiStationConfig.password;
  doc["enabled"] = wifiStationConfig.enabled;
  
  if (serializeJson(doc, file) == 0) {
    debugPrintln("DEBUG: Failed to write WiFi config to file");
  }
  
  file.close();
}

void loadWiFiConfig() {
  if (!SPIFFS.exists("/wifi_config.json")) {
    debugPrintln("DEBUG: WiFi config file not found, using defaults");
    strcpy(wifiStationConfig.ssid, "");
    strcpy(wifiStationConfig.password, "");
    wifiStationConfig.enabled = false;
    saveWiFiConfig();
    return;
  }
  
  File file = SPIFFS.open("/wifi_config.json", FILE_READ);
  if (!file) {
    debugPrintln("DEBUG: Failed to open WiFi config file for reading");
    return;
  }
  
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    debugPrintf("DEBUG: Failed to parse WiFi config JSON: %s\n", error.c_str());
    return;
  }

  // Wifi Station Credentials ssid/password
  strlcpy(wifiStationConfig.ssid, doc["ssid"] | "", sizeof(wifiStationConfig.ssid));
  strlcpy(wifiStationConfig.password, doc["password"] | "", sizeof(wifiStationConfig.password));
  wifiStationConfig.enabled = doc["enabled"] | false;
}

void* createWiFiTestParam(const char* ssid, const char* password) {
  size_t ssidLen = strlen(ssid);
  size_t passLen = strlen(password);
  char* param = (char*)malloc(ssidLen + passLen + 2); // +2 for null terminators
  
  if (param) {
    strcpy(param, ssid);
    strcpy(param + ssidLen + 1, password);
  }
  
  return param;
}