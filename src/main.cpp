#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "PinConfig.h"
#include "TestMode.h"
#include <time.h>

// WiFi settings
const char* ssid = "ES32A08-Setup"; // Default AP SSID
const char* password = "password";  // Default AP password

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Global variables (matching the existing TestMode.cpp)
extern volatile uint8_t relayState;  // Declared in TestMode.cpp
extern uint8_t Read_74HC165();       // Function from TestMode.cpp
extern uint8_t Get_DI_Value();       // Function from TestMode.cpp

// Add global variables for analog values
float voltageValues[4] = {0.0, 0.0, 0.0, 0.0};
float currentValues[4] = {0.0, 0.0, 0.0, 0.0};
bool buttonStates[4] = {false, false, false, false};
bool inputStates[8] = {false, false, false, false, false, false, false, false};
// You will need this mutex at the top of your main.cpp file with other global variables:
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Helper structure for manual watering task parameters
struct ManualWateringParams {
  int relay;
  int duration;
};
/*************************************************WIFI ************************************************/

// WiFi station mode credentials - stored in a config file
struct WiFiConfig {
  char ssid[32];
  char password[64];
  bool enabled;
};

// Task data structure for WiFi test
struct WiFiTestParams {
  AsyncWebServerRequest* request;
  char* ssid;
  char* password;
  bool wasEnabled;
  char* oldSsid;
  char* oldPassword;
};

WiFiConfig wifiStationConfig;
bool stationConnected = false;

// First, make sure the debug functions are declared
extern void debugPrintln(const char* message);
extern void debugPrintf(const char* format, ...);

// Save WiFi configuration to SPIFFS
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

// Load WiFi configuration from SPIFFS
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

// WiFi event handler
void WiFiEventHandler(WiFiEvent_t event) {
  switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      debugPrintln("DEBUG: WiFi connected! IP address: ");
      debugPrintln(WiFi.localIP().toString().c_str());
      stationConnected = true;
      
      // Now that we have Internet, sync time with NTP
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      break;
      
    case SYSTEM_EVENT_STA_DISCONNECTED:
      debugPrintln("DEBUG: WiFi lost connection");
      stationConnected = false;
      break;
      
    default:
      break;
  }
}

// Setup dual WiFi mode (AP + STA if configured)
void setupDualWiFi() {
  // Load WiFi configuration
  loadWiFiConfig();
  
  // Set WiFi event handler
  WiFi.onEvent(WiFiEventHandler);
  
  // Always start AP mode
  // WiFi.softAP(AP_SSID, AP_PASSWORD);
  WiFi.softAP("ES32A08-Setup","password");
  IPAddress apIP = WiFi.softAPIP();
  debugPrintln("DEBUG: AP Mode IP Address: ");
  debugPrintln(apIP.toString().c_str());
  
  // If STA mode is enabled, connect to WiFi network
  if (wifiStationConfig.enabled && strlen(wifiStationConfig.ssid) > 0) {
    debugPrintf("DEBUG: Connecting to WiFi: %s\n", wifiStationConfig.ssid);
    WiFi.mode(WIFI_AP_STA);  // Set dual mode
    WiFi.begin(wifiStationConfig.ssid, wifiStationConfig.password);
    
    // Don't wait for connection - it will happen in the background
    // WiFi events will be handled by the event handler
  } else {
    WiFi.mode(WIFI_AP);  // AP mode only
  }
}

// Handler for getting WiFi status
void handleGetWiFiStatus(AsyncWebServerRequest *request) {
  debugPrintln("DEBUG: API request received: /api/wifi/status");
  
  DynamicJsonDocument doc(512);
  
  // AP mode info is always available
  doc["apSsid"] = ssid;
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
// Handler for setting WiFi credentials
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

// Handler for testing a WiFi connection
void handleTestWiFiConnection(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("DEBUG: API request received: /api/wifi/test");
  
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  const char* ssidValue = doc.containsKey("ssid") ? doc["ssid"].as<const char*>() : "";
  const char* passwordValue = doc.containsKey("password") ? doc["password"].as<const char*>() : "";
  
  // Validate SSID
  if (strlen(ssidValue) == 0 || strlen(ssidValue) > 31) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid SSID. Must be between 1 and 31 characters.\"}");
    return;
  }
  
  debugPrintf("DEBUG: Testing WiFi connection to %s\n", ssidValue);
  
  // Remember current settings
  bool wasEnabled = wifiStationConfig.enabled;
  char oldSsid[32];
  char oldPassword[64];
  strlcpy(oldSsid, wifiStationConfig.ssid, sizeof(oldSsid));
  strlcpy(oldPassword, wifiStationConfig.password, sizeof(oldPassword));
  
  // Create a task to handle the connection test
  xTaskCreate(
    [](void* parameter) {
      // Extract parameters
      struct {
        AsyncWebServerRequest* request;
        const char* ssid;
        const char* password;
        bool wasEnabled;
        const char* oldSsid;
        const char* oldPassword;
      }* params = (decltype(params))parameter;
      
      DynamicJsonDocument responseDoc(256);
      String responseStr;
      
      // Start connection test
      WiFi.disconnect();
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(params->ssid, params->password);
      
      // Wait for connection with timeout
      int timeout = 0;
      while (WiFi.status() != WL_CONNECTED && timeout < 20) {  // 10 second timeout
        delay(500);
        timeout++;
      }
      
      // Check result
      if (WiFi.status() == WL_CONNECTED) {
        debugPrintln("DEBUG: Test connection successful! IP address: ");
        debugPrintln(WiFi.localIP().toString().c_str());
        
        responseDoc["status"] = "success";
        responseDoc["message"] = "Connected successfully";
        responseDoc["ip"] = WiFi.localIP().toString();
        responseDoc["rssi"] = WiFi.RSSI();
      } else {
        String errorMsg;
        switch (WiFi.status()) {
          case WL_NO_SSID_AVAIL:
            errorMsg = "Network not found";
            break;
          case WL_CONNECT_FAILED:
            errorMsg = "Invalid password";
            break;
          case WL_DISCONNECTED:
            errorMsg = "Connection failed";
            break;
          default:
            errorMsg = "Unknown error";
            break;
        }
        
        debugPrintf("DEBUG: Test connection failed: %s (status: %d)\n", 
                  errorMsg.c_str(), WiFi.status());
        
        responseDoc["status"] = "error";
        responseDoc["message"] = errorMsg;
      }
      
      // Send response
      serializeJson(responseDoc, responseStr);
      params->request->send(200, "application/json", responseStr);
      
      // Restore previous connection if needed
      if (params->wasEnabled) {
        WiFi.disconnect();
        WiFi.begin(params->oldSsid, params->oldPassword);
      } else {
        WiFi.mode(WIFI_AP);  // AP mode only
      }
      
      // Free parameter memory
      delete[] params->ssid;
      delete[] params->password;
      delete[] params->oldSsid;
      delete[] params->oldPassword;
      delete params;
      
      vTaskDelete(NULL);
    },
    "WiFiTestTask",
    4096,
    new WiFiTestParams {
      request,
      strdup(ssidValue),
      strdup(passwordValue),
      wasEnabled,
      strdup(oldSsid),
      strdup(oldPassword)
    },
    1,
    NULL
  );
  
  // Don't send response here, the task will handle it
}

// Add these routes to your server setup in initServer() or setup()
void initWiFiRoutes() {
  // Serve the WiFi configuration page
  server.on("/wifi.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    debugPrintln("DEBUG: Serving wifi.html");
    request->send(SPIFFS, "/wifi.html", String(), false);
  });
  
  // Serve the WiFi JavaScript
  server.on("/js/wifi.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    debugPrintln("DEBUG: Serving wifi.js");
    request->send(SPIFFS, "/js/wifi.js", "text/javascript");
  });
  
  // API endpoints
  server.on("/api/wifi/status", HTTP_GET, handleGetWiFiStatus);
  
  server.on("/api/wifi/config", HTTP_POST, 
    [](AsyncWebServerRequest *request) {},
    NULL,
    handleSetWiFiCredentials
  );
 

  server.on("/api/wifi/test", HTTP_POST, 
    [](AsyncWebServerRequest *request) {
      // This callback is called after the request is handled
      // Don't send response here, it's handled in the next callback
    },
    NULL,  // Upload handler (not needed for JSON)
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      // This is where you handle the JSON data
      handleTestWiFiConnection(request, data, len, index, total);
    }
  );
}

/*************************************************WIFI ************************************************/

// For MODBUS communications
#define MODBUS_BUFFER_SIZE 256
uint8_t modbusRequestBuffer[MODBUS_BUFFER_SIZE];
uint8_t modbusResponseBuffer[MODBUS_BUFFER_SIZE];
bool rs485Initialized = false;

// UART mode tracking
enum UartMode {
  UART_MODE_DEBUG,
  UART_MODE_RS485
};

UartMode currentUartMode = UART_MODE_DEBUG;

// Initialize flags for task completion
volatile bool initTestModeComplete = false;

// Switch UART between debug and RS485 modes
void switchToDebugMode() {
  if (currentUartMode == UART_MODE_DEBUG) {
    return;  // Already in debug mode
  }
  
  // Flush any pending data
  Serial.flush();
  
  // Reconfigure for debug
  Serial.begin(115200);
  
  currentUartMode = UART_MODE_DEBUG;
  delay(10);  // Small delay for UART to stabilize
}

// First, make sure the debug functions are declared
extern void debugPrintln(const char* message);
extern void debugPrintf(const char* format, ...);

void handleSaveSchedulerState(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// This task will continuously update the relay outputs
void vRelayUpdateTask(void *pvParameters) {
  debugPrintln("DEBUG: Relay update task started");
  
  // Local variables for shift register control
  static const uint8_t ALL_OFF = 0x00;
  
  // Local variable to track relay state changes for debugging
  uint8_t lastRelayState = 0xFF; // Initialize to a different value to force first update
  
  for (;;) {
    // Check if relay state has changed
    if (relayState != lastRelayState) {
      lastRelayState = relayState;
      debugPrintf("DEBUG: Relay state changed to 0x%02X\n", relayState);
    }
    
    // Send the current relay state to the shift register
    // We need to disable interrupts briefly to ensure the shift register operations aren't interrupted
    portENTER_CRITICAL(&mux);
    
    // Send relay state byte directly
    digitalWrite(SH595_LATCH, LOW);
    
    // Send relay state as first byte
    for (uint8_t i = 0; i < 8; i++) {
      digitalWrite(SH595_DATA, (relayState & (0x80 >> i)) ? HIGH : LOW);
      digitalWrite(SH595_CLOCK, LOW);
      digitalWrite(SH595_CLOCK, HIGH);
    }
    
    // Send zero for other bytes (display control) to avoid interference
    for (uint8_t i = 0; i < 16; i++) { // 16 more bits (2 bytes) for display control
      digitalWrite(SH595_DATA, LOW);
      digitalWrite(SH595_CLOCK, LOW);
      digitalWrite(SH595_CLOCK, HIGH);
    }
    
    digitalWrite(SH595_LATCH, HIGH);
    
    portEXIT_CRITICAL(&mux);
    
    // Brief debug message every 10 seconds for monitoring
    static uint32_t lastDebugTime = 0;
    if (millis() - lastDebugTime > 10000) {
      lastDebugTime = millis();
      debugPrintf("DEBUG: Relay update task running, current state: 0x%02X\n", relayState);
    }
    
    // Update at a reasonable rate
    vTaskDelay(pdMS_TO_TICKS(50)); // 50ms delay (20Hz update rate)
  }
}

void switchToRS485Mode() {
  if (currentUartMode == UART_MODE_RS485) {
    return;  // Already in RS485 mode
  }
  
  // Flush any pending debug data
  Serial.flush();
  
  // Reconfigure for RS485
  Serial.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW);  // Default to receive mode
  
  currentUartMode = UART_MODE_RS485;
  delay(10);  // Small delay for UART to stabilize
}

// Safe debug print that ensures we're in debug mode
void debugPrint(const char* message) {
  switchToDebugMode();
  Serial.print(message);
}

void debugPrintln(const char* message) {
  switchToDebugMode();
  Serial.println(message);
}

void debugPrintf(const char* format, ...) {
  switchToDebugMode();
  
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsprintf(buffer, format, args);
  va_end(args);
  
  Serial.print(buffer);
}

// RS485 transmit control
void rs485Transmit(bool enable) {
  digitalWrite(RS485_DE, enable);
  delay(1); // Short delay for line settling
  
  // No debug print here to avoid switching UARTs during critical timing
}

// Task for initializing 74HC595 with timeout
void initTestModeTask(void *pvParameters) {
  debugPrintln("DEBUG: In initTestMode task...");
  
  // Initialize the shift register pins directly
  pinMode(SH595_DATA, OUTPUT);
  pinMode(SH595_CLOCK, OUTPUT);
  pinMode(SH595_LATCH, OUTPUT);
  pinMode(SH595_OE, OUTPUT);
  
  // Try to clear display - simplified version
  digitalWrite(SH595_LATCH, LOW);
  for (int i = 0; i < 24; i++) {
    digitalWrite(SH595_DATA, LOW);
    digitalWrite(SH595_CLOCK, HIGH);
    digitalWrite(SH595_CLOCK, LOW);
  }
  digitalWrite(SH595_LATCH, HIGH);
  digitalWrite(SH595_OE, LOW);  // Enable outputs
  
  // Try a more careful approach to the original function
  // but with timeouts to prevent hanging
  try {
    debugPrintln("DEBUG: Running original initTestMode...");
    unsigned long startTime = millis();
    initTestMode();
    debugPrintln("DEBUG: initTestMode completed successfully");
  } catch (...) {
    debugPrintln("DEBUG: Exception in initTestMode");
  }
  
  initTestModeComplete = true;
  vTaskDelete(NULL);
}

// Global constants
#define SCHEDULER_FILE "/scheduler.json"
#define MAX_SCHEDULES 50
#define MAX_EVENTS 20
#define MAX_TEMPLATES 10

// Scheduler state structure - matches the JavaScript state object
struct SchedulerState {
  struct {
    String lightsOnTime;
    String lightsOffTime;
  } lightSchedule;
  
  struct Schedule {
    String id;
    int frequency;    // In minutes
    int duration;     // In seconds
    int relay;        // Relay number (0-7)
  };
  
  struct Event {
    String id;
    String time;      // HH:MM format
    int duration;     // In seconds
    int relay;        // Relay number (0-7)
  };
  
  struct Template {
    String id;
    String name;
    String lightsOnTime;
    String lightsOffTime;
    Schedule lightsOnSchedules[MAX_SCHEDULES];
    int lightsOnSchedulesCount;
    Schedule lightsOffSchedules[MAX_SCHEDULES];
    int lightsOffSchedulesCount;
    Event customEvents[MAX_EVENTS];
    int customEventsCount;
  };
  
  Schedule lightsOnSchedules[MAX_SCHEDULES];
  int lightsOnSchedulesCount;
  
  Schedule lightsOffSchedules[MAX_SCHEDULES];
  int lightsOffSchedulesCount;
  
  Event customEvents[MAX_EVENTS];
  int customEventsCount;
  
  Template templates[MAX_TEMPLATES];
  int templatesCount;
  
  bool isActive;
  String currentLightCondition;  // "Lights On", "Lights Off", or "Unknown"
  
  struct NextEvent {
    String time;
    int duration;
    int relay;
  } nextEvent;
  
  bool hasNextEvent;
};

// Global scheduler state
SchedulerState schedulerState;

// Mutex for scheduler access
portMUX_TYPE schedulerMutex = portMUX_INITIALIZER_UNLOCKED;

// Forward declarations
void saveSchedulerState();
void loadSchedulerState();
void calculateNextEvent();
void checkSchedulerStatus();
void startSchedulerTask();
void stopSchedulerTask();
void executeWatering(int relay, int duration);

// Handle for the scheduler task
TaskHandle_t schedulerTaskHandle = NULL;

// First, make sure the debug functions are declared
extern void debugPrintln(const char* message);
extern void debugPrintf(const char* format, ...);

// This task runs the scheduler
void schedulerTask(void *parameter) {
  debugPrintln("DEBUG: Scheduler task started");
  
  // Run continuously while active
  for (;;) {
    // Check current time
    time_t now;
    struct tm timeinfo;
    bool timeIsValid = false;
    
    if (getLocalTime(&timeinfo)) {
      time(&now);
      timeIsValid = true;
      
      // Format current time as HH:MM
      char timeStr[6];
      strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
      String currentTime = String(timeStr);
      
      // If time is valid, update light condition
      portENTER_CRITICAL(&schedulerMutex);
      
      if (timeIsValid) {
        // Convert light schedule times to tm structures for comparison
        struct tm lightsOnTm = {};
        struct tm lightsOffTm = {};
        
        // Parse lightsOnTime
        sscanf(schedulerState.lightSchedule.lightsOnTime.c_str(), "%d:%d", 
              &lightsOnTm.tm_hour, &lightsOnTm.tm_min);
        lightsOnTm.tm_year = timeinfo.tm_year;
        lightsOnTm.tm_mon = timeinfo.tm_mon;
        lightsOnTm.tm_mday = timeinfo.tm_mday;
        
        // Parse lightsOffTime
        sscanf(schedulerState.lightSchedule.lightsOffTime.c_str(), "%d:%d", 
              &lightsOffTm.tm_hour, &lightsOffTm.tm_min);
        lightsOffTm.tm_year = timeinfo.tm_year;
        lightsOffTm.tm_mon = timeinfo.tm_mon;
        lightsOffTm.tm_mday = timeinfo.tm_mday;
        
        // Convert to time_t for comparison
        time_t lightsOnTime = mktime(&lightsOnTm);
        time_t lightsOffTime = mktime(&lightsOffTm);
        
        // Handle case where lights off is earlier than lights on (spans midnight)
        if (lightsOffTime < lightsOnTime) {
          if (now >= lightsOnTime || now < lightsOffTime) {
            schedulerState.currentLightCondition = "Lights On";
          } else {
            schedulerState.currentLightCondition = "Lights Off";
          }
        } else {
          if (now >= lightsOnTime && now < lightsOffTime) {
            schedulerState.currentLightCondition = "Lights On";
          } else {
            schedulerState.currentLightCondition = "Lights Off";
          }
        }
      }
      
      // Check all schedules and events
      
      // First, check custom events
      for (int i = 0; i < schedulerState.customEventsCount; i++) {
        if (currentTime == schedulerState.customEvents[i].time) {
          int relay = schedulerState.customEvents[i].relay;
          int duration = schedulerState.customEvents[i].duration;
          
          debugPrintf("DEBUG: Executing custom event for relay %d with duration %d seconds\n", 
                     relay, duration);
          
          // Execute outside of critical section
          portEXIT_CRITICAL(&schedulerMutex);
          executeWatering(relay, duration);
          portENTER_CRITICAL(&schedulerMutex);
        }
      }
      
      // Check periodic schedules
      if (schedulerState.currentLightCondition == "Lights On") {
        // For lights-on schedules, check if current minute is divisible by frequency
        for (int i = 0; i < schedulerState.lightsOnSchedulesCount; i++) {
          int frequency = schedulerState.lightsOnSchedules[i].frequency;
          
          // Calculate minutes since lights on
          struct tm lightsOnTm = {};
          sscanf(schedulerState.lightSchedule.lightsOnTime.c_str(), "%d:%d", 
                &lightsOnTm.tm_hour, &lightsOnTm.tm_min);
          lightsOnTm.tm_year = timeinfo.tm_year;
          lightsOnTm.tm_mon = timeinfo.tm_mon;
          lightsOnTm.tm_mday = timeinfo.tm_mday;
          
          time_t lightsOnTime = mktime(&lightsOnTm);
          int minutesSinceLightsOn = (now - lightsOnTime) / 60;
          
          if (minutesSinceLightsOn >= 0 && minutesSinceLightsOn % frequency == 0) {
            // This is a watering time
            int relay = schedulerState.lightsOnSchedules[i].relay;
            int duration = schedulerState.lightsOnSchedules[i].duration;
            
            debugPrintf("DEBUG: Executing lights-on schedule for relay %d with duration %d seconds\n", 
                      relay, duration);
            
            // Execute outside of critical section
            portEXIT_CRITICAL(&schedulerMutex);
            executeWatering(relay, duration);
            portENTER_CRITICAL(&schedulerMutex);
          }
        }
      } else if (schedulerState.currentLightCondition == "Lights Off") {
        // For lights-off schedules, check if current minute is divisible by frequency
        for (int i = 0; i < schedulerState.lightsOffSchedulesCount; i++) {
          int frequency = schedulerState.lightsOffSchedules[i].frequency;
          
          // Calculate minutes since lights off
          struct tm lightsOffTm = {};
          sscanf(schedulerState.lightSchedule.lightsOffTime.c_str(), "%d:%d", 
                &lightsOffTm.tm_hour, &lightsOffTm.tm_min);
          lightsOffTm.tm_year = timeinfo.tm_year;
          lightsOffTm.tm_mon = timeinfo.tm_mon;
          lightsOffTm.tm_mday = timeinfo.tm_mday;
          
          time_t lightsOffTime = mktime(&lightsOffTm);
          
          // Handle case where lights off is earlier than current time (spans midnight)
          if (lightsOffTime > now) {
            lightsOffTm.tm_mday -= 1;
            lightsOffTime = mktime(&lightsOffTm);
          }
          
          int minutesSinceLightsOff = (now - lightsOffTime) / 60;
          
          if (minutesSinceLightsOff >= 0 && minutesSinceLightsOff % frequency == 0) {
            // This is a watering time
            int relay = schedulerState.lightsOffSchedules[i].relay;
            int duration = schedulerState.lightsOffSchedules[i].duration;
            
            debugPrintf("DEBUG: Executing lights-off schedule for relay %d with duration %d seconds\n", 
                      relay, duration);
            
            // Execute outside of critical section
            portEXIT_CRITICAL(&schedulerMutex);
            executeWatering(relay, duration);
            portENTER_CRITICAL(&schedulerMutex);
          }
        }
      }
      
      // Recalculate next event
      calculateNextEvent();
      
      portEXIT_CRITICAL(&schedulerMutex);
    } else {
      debugPrintln("DEBUG: Failed to get local time");
      portEXIT_CRITICAL(&schedulerMutex);
    }
    
    // Check every minute
    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

// Function to start the scheduler task
void startSchedulerTask() {
  if (schedulerTaskHandle == NULL) {
    xTaskCreatePinnedToCore(
      schedulerTask,       // Function to implement the task
      "SchedulerTask",     // Name of the task
      8192,                // Stack size in words
      NULL,                // Task input parameter
      1,                   // Priority of the task
      &schedulerTaskHandle, // Task handle
      1                    // Core (use core 1 like other tasks)
    );
    
    schedulerState.isActive = true;
    debugPrintln("DEBUG: Scheduler task started");
  } else {
    debugPrintln("DEBUG: Scheduler task already running");
  }
}

// Function to stop the scheduler task
void stopSchedulerTask() {
  if (schedulerTaskHandle != NULL) {
    vTaskDelete(schedulerTaskHandle);
    schedulerTaskHandle = NULL;
    schedulerState.isActive = false;
    debugPrintln("DEBUG: Scheduler task stopped");
  } else {
    debugPrintln("DEBUG: Scheduler task not running");
  }
}

// Execute watering by turning relay on for specified duration
void executeWatering(int relay, int duration) {
  if (relay < 0 || relay > 7) {
    debugPrintln("DEBUG: Invalid relay number for watering");
    return;
  }
  
  debugPrintf("DEBUG: Starting watering on relay %d for %d seconds\n", relay, duration);
  
  // Set relay bit
  uint8_t oldRelayState = relayState;
  relayState |= (1 << relay);
  
  debugPrintf("DEBUG: Relay state changed: 0x%02X -> 0x%02X\n", oldRelayState, relayState);
  
  // Wait for duration
  delay(duration * 1000);
  
  // Clear relay bit
  oldRelayState = relayState;
  relayState &= ~(1 << relay);
  
  debugPrintf("DEBUG: Relay state changed: 0x%02X -> 0x%02X\n", oldRelayState, relayState);
}

// Calculate the next scheduled event
void calculateNextEvent() {
  // Implementation depends on current time and scheduling logic
  // This is a simplified version
  time_t now;
  struct tm timeinfo;
  time(&now);
  
  // Initialize earliest event time to a far future time
  time_t earliestEventTime = now + 86400; // 24 hours from now
  int earliestEventDuration = 0;
  int earliestEventRelay = 0;
  bool foundEvent = false;
  
  // Check if we have local time
  if (getLocalTime(&timeinfo)) {
    // Format current time as HH:MM
    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    String currentTime = String(timeStr);
    
    // Check custom events
    for (int i = 0; i < schedulerState.customEventsCount; i++) {
      // Parse event time
      struct tm eventTm = {};
      sscanf(schedulerState.customEvents[i].time.c_str(), "%d:%d", 
            &eventTm.tm_hour, &eventTm.tm_min);
      eventTm.tm_year = timeinfo.tm_year;
      eventTm.tm_mon = timeinfo.tm_mon;
      eventTm.tm_mday = timeinfo.tm_mday;
      
      // If event time is earlier than current time, it's for tomorrow
      if (eventTm.tm_hour < timeinfo.tm_hour || 
          (eventTm.tm_hour == timeinfo.tm_hour && eventTm.tm_min <= timeinfo.tm_min)) {
        eventTm.tm_mday += 1;
      }
      
      time_t eventTime = mktime(&eventTm);
      
      if (eventTime < earliestEventTime) {
        earliestEventTime = eventTime;
        earliestEventDuration = schedulerState.customEvents[i].duration;
        earliestEventRelay = schedulerState.customEvents[i].relay;
        foundEvent = true;
      }
    }
    
    // Process periodic schedules
    // This is more complex and requires calculating the next occurrence
    // of each periodic schedule based on its frequency and the light schedule
    
    if (foundEvent) {
      char nextTimeStr[6];
      struct tm* eventTimeinfo = localtime(&earliestEventTime);
      strftime(nextTimeStr, sizeof(nextTimeStr), "%H:%M", eventTimeinfo);
      
      schedulerState.nextEvent.time = String(nextTimeStr);
      schedulerState.nextEvent.duration = earliestEventDuration;
      schedulerState.nextEvent.relay = earliestEventRelay;
      schedulerState.hasNextEvent = true;
    } else {
      schedulerState.hasNextEvent = false;
    }
  }
}

// Handler for saving scheduler state
void handleSaveSchedulerState(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("DEBUG: API request received: /api/scheduler/save");
  
  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  portENTER_CRITICAL(&schedulerMutex);
  
  // Light schedule
  if (doc.containsKey("lightSchedule")) {
    JsonObject lightSchedule = doc["lightSchedule"];
    schedulerState.lightSchedule.lightsOnTime = lightSchedule["lightsOnTime"].as<String>();
    schedulerState.lightSchedule.lightsOffTime = lightSchedule["lightsOffTime"].as<String>();
  }
  
  // Lights On schedules
  if (doc.containsKey("lightsOnSchedules")) {
    JsonArray lightsOnSchedules = doc["lightsOnSchedules"];
    schedulerState.lightsOnSchedulesCount = min((int)lightsOnSchedules.size(), MAX_SCHEDULES);
    
    for (int i = 0; i < schedulerState.lightsOnSchedulesCount; i++) {
      JsonObject schedule = lightsOnSchedules[i];
      schedulerState.lightsOnSchedules[i].id = schedule["id"].as<String>();
      schedulerState.lightsOnSchedules[i].frequency = schedule["frequency"].as<int>();
      schedulerState.lightsOnSchedules[i].duration = schedule["duration"].as<int>();
      schedulerState.lightsOnSchedules[i].relay = schedule["relay"].as<int>();
    }
  } else {
    schedulerState.lightsOnSchedulesCount = 0;
  }
  
  // Lights Off schedules
  if (doc.containsKey("lightsOffSchedules")) {
    JsonArray lightsOffSchedules = doc["lightsOffSchedules"];
    schedulerState.lightsOffSchedulesCount = min((int)lightsOffSchedules.size(), MAX_SCHEDULES);
    
    for (int i = 0; i < schedulerState.lightsOffSchedulesCount; i++) {
      JsonObject schedule = lightsOffSchedules[i];
      schedulerState.lightsOffSchedules[i].id = schedule["id"].as<String>();
      schedulerState.lightsOffSchedules[i].frequency = schedule["frequency"].as<int>();
      schedulerState.lightsOffSchedules[i].duration = schedule["duration"].as<int>();
      schedulerState.lightsOffSchedules[i].relay = schedule["relay"].as<int>();
    }
  } else {
    schedulerState.lightsOffSchedulesCount = 0;
  }
  
  // Custom events
  if (doc.containsKey("customEvents")) {
    JsonArray customEvents = doc["customEvents"];
    schedulerState.customEventsCount = min((int)customEvents.size(), MAX_EVENTS);
    
    for (int i = 0; i < schedulerState.customEventsCount; i++) {
      JsonObject event = customEvents[i];
      schedulerState.customEvents[i].id = event["id"].as<String>();
      schedulerState.customEvents[i].time = event["time"].as<String>();
      schedulerState.customEvents[i].duration = event["duration"].as<int>();
      schedulerState.customEvents[i].relay = event["relay"].as<int>();
    }
  } else {
    schedulerState.customEventsCount = 0;
  }
  
  // Templates
  if (doc.containsKey("templates")) {
    JsonArray templates = doc["templates"];
    schedulerState.templatesCount = min((int)templates.size(), MAX_TEMPLATES);
    
    for (int i = 0; i < schedulerState.templatesCount; i++) {
      JsonObject templateObj = templates[i];
      schedulerState.templates[i].id = templateObj["id"].as<String>();
      schedulerState.templates[i].name = templateObj["name"].as<String>();
      
      if (templateObj.containsKey("lightSchedule")) {
        JsonObject templateLightSchedule = templateObj["lightSchedule"];
        schedulerState.templates[i].lightsOnTime = templateLightSchedule["lightsOnTime"].as<String>();
        schedulerState.templates[i].lightsOffTime = templateLightSchedule["lightsOffTime"].as<String>();
      } else {
        schedulerState.templates[i].lightsOnTime = schedulerState.lightSchedule.lightsOnTime;
        schedulerState.templates[i].lightsOffTime = schedulerState.lightSchedule.lightsOffTime;
      }
      
      // Template lights on schedules
      if (templateObj.containsKey("lightsOnSchedules")) {
        JsonArray templateLightsOnSchedules = templateObj["lightsOnSchedules"];
        schedulerState.templates[i].lightsOnSchedulesCount = min((int)templateLightsOnSchedules.size(), MAX_SCHEDULES);
        
        for (int j = 0; j < schedulerState.templates[i].lightsOnSchedulesCount; j++) {
          JsonObject schedule = templateLightsOnSchedules[j];
          schedulerState.templates[i].lightsOnSchedules[j].id = schedule["id"].as<String>();
          schedulerState.templates[i].lightsOnSchedules[j].frequency = schedule["frequency"].as<int>();
          schedulerState.templates[i].lightsOnSchedules[j].duration = schedule["duration"].as<int>();
          schedulerState.templates[i].lightsOnSchedules[j].relay = schedule["relay"].as<int>();
        }
      } else {
        schedulerState.templates[i].lightsOnSchedulesCount = 0;
      }
      
      // Template lights off schedules
      if (templateObj.containsKey("lightsOffSchedules")) {
        JsonArray templateLightsOffSchedules = templateObj["lightsOffSchedules"];
        schedulerState.templates[i].lightsOffSchedulesCount = min((int)templateLightsOffSchedules.size(), MAX_SCHEDULES);
        
        for (int j = 0; j < schedulerState.templates[i].lightsOffSchedulesCount; j++) {
          JsonObject schedule = templateLightsOffSchedules[j];
          schedulerState.templates[i].lightsOffSchedules[j].id = schedule["id"].as<String>();
          schedulerState.templates[i].lightsOffSchedules[j].frequency = schedule["frequency"].as<int>();
          schedulerState.templates[i].lightsOffSchedules[j].duration = schedule["duration"].as<int>();
          schedulerState.templates[i].lightsOffSchedules[j].relay = schedule["relay"].as<int>();
        }
      } else {
        schedulerState.templates[i].lightsOffSchedulesCount = 0;
      }
      
      // Template custom events
      if (templateObj.containsKey("customEvents")) {
        JsonArray templateCustomEvents = templateObj["customEvents"];
        schedulerState.templates[i].customEventsCount = min((int)templateCustomEvents.size(), MAX_EVENTS);
        
        for (int j = 0; j < schedulerState.templates[i].customEventsCount; j++) {
          JsonObject event = templateCustomEvents[j];
          schedulerState.templates[i].customEvents[j].id = event["id"].as<String>();
          schedulerState.templates[i].customEvents[j].time = event["time"].as<String>();
          schedulerState.templates[i].customEvents[j].duration = event["duration"].as<int>();
          schedulerState.templates[i].customEvents[j].relay = event["relay"].as<int>();
        }
      } else {
        schedulerState.templates[i].customEventsCount = 0;
      }
    }
  }
  
  // isActive stays the same unless explicitly changed
  if (doc.containsKey("isActive")) {
    bool newActiveState = doc["isActive"].as<bool>();
    
    // Only change scheduler state if it's different
    if (newActiveState != schedulerState.isActive) {
      schedulerState.isActive = newActiveState;
      
      // Start or stop the scheduler task
      if (newActiveState) {
        // Exit the critical section before starting the task
        portEXIT_CRITICAL(&schedulerMutex);
        startSchedulerTask();
        portENTER_CRITICAL(&schedulerMutex);
      } else {
        // Exit the critical section before stopping the task
        portEXIT_CRITICAL(&schedulerMutex);
        stopSchedulerTask();
        portENTER_CRITICAL(&schedulerMutex);
      }
    }
  }
  
  // Recalculate next event
  calculateNextEvent();
  
  // Save to file
  portEXIT_CRITICAL(&schedulerMutex);
  saveSchedulerState();
  
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler state saved\"}");
}

// Save scheduler state to file
void saveSchedulerState() {
  debugPrintln("DEBUG: Saving scheduler state to file");
  
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_WRITE);
  if (!file) {
    debugPrintln("DEBUG: Failed to open scheduler file for writing");
    return;
  }
  
  DynamicJsonDocument doc(16384); // Adjust size based on your needs
  
  // Light schedule
  JsonObject lightSchedule = doc.createNestedObject("lightSchedule");
  lightSchedule["lightsOnTime"] = schedulerState.lightSchedule.lightsOnTime;
  lightSchedule["lightsOffTime"] = schedulerState.lightSchedule.lightsOffTime;
  
  // Lights On schedules
  JsonArray lightsOnSchedules = doc.createNestedArray("lightsOnSchedules");
  for (int i = 0; i < schedulerState.lightsOnSchedulesCount; i++) {
    JsonObject schedule = lightsOnSchedules.createNestedObject();
    schedule["id"] = schedulerState.lightsOnSchedules[i].id;
    schedule["frequency"] = schedulerState.lightsOnSchedules[i].frequency;
    schedule["duration"] = schedulerState.lightsOnSchedules[i].duration;
    schedule["relay"] = schedulerState.lightsOnSchedules[i].relay;
  }
  
  // Lights Off schedules
  JsonArray lightsOffSchedules = doc.createNestedArray("lightsOffSchedules");
  for (int i = 0; i < schedulerState.lightsOffSchedulesCount; i++) {
    JsonObject schedule = lightsOffSchedules.createNestedObject();
    schedule["id"] = schedulerState.lightsOffSchedules[i].id;
    schedule["frequency"] = schedulerState.lightsOffSchedules[i].frequency;
    schedule["duration"] = schedulerState.lightsOffSchedules[i].duration;
    schedule["relay"] = schedulerState.lightsOffSchedules[i].relay;
  }
  
  // Custom events
  JsonArray customEvents = doc.createNestedArray("customEvents");
  for (int i = 0; i < schedulerState.customEventsCount; i++) {
    JsonObject event = customEvents.createNestedObject();
    event["id"] = schedulerState.customEvents[i].id;
    event["time"] = schedulerState.customEvents[i].time;
    event["duration"] = schedulerState.customEvents[i].duration;
    event["relay"] = schedulerState.customEvents[i].relay;
  }
  
  // Templates
  JsonArray templates = doc.createNestedArray("templates");
  for (int i = 0; i < schedulerState.templatesCount; i++) {
    JsonObject templateObj = templates.createNestedObject();
    templateObj["id"] = schedulerState.templates[i].id;
    templateObj["name"] = schedulerState.templates[i].name;
    
    JsonObject templateLightSchedule = templateObj.createNestedObject("lightSchedule");
    templateLightSchedule["lightsOnTime"] = schedulerState.templates[i].lightsOnTime;
    templateLightSchedule["lightsOffTime"] = schedulerState.templates[i].lightsOffTime;
    
    // Template lights on schedules
    JsonArray templateLightsOnSchedules = templateObj.createNestedArray("lightsOnSchedules");
    for (int j = 0; j < schedulerState.templates[i].lightsOnSchedulesCount; j++) {
      JsonObject schedule = templateLightsOnSchedules.createNestedObject();
      schedule["id"] = schedulerState.templates[i].lightsOnSchedules[j].id;
      schedule["frequency"] = schedulerState.templates[i].lightsOnSchedules[j].frequency;
      schedule["duration"] = schedulerState.templates[i].lightsOnSchedules[j].duration;
      schedule["relay"] = schedulerState.templates[i].lightsOnSchedules[j].relay;
    }
    
    // Template lights off schedules
    JsonArray templateLightsOffSchedules = templateObj.createNestedArray("lightsOffSchedules");
    for (int j = 0; j < schedulerState.templates[i].lightsOffSchedulesCount; j++) {
      JsonObject schedule = templateLightsOffSchedules.createNestedObject();
      schedule["id"] = schedulerState.templates[i].lightsOffSchedules[j].id;
      schedule["frequency"] = schedulerState.templates[i].lightsOffSchedules[j].frequency;
      schedule["duration"] = schedulerState.templates[i].lightsOffSchedules[j].duration;
      schedule["relay"] = schedulerState.templates[i].lightsOffSchedules[j].relay;
    }
    
    // Template custom events
    JsonArray templateCustomEvents = templateObj.createNestedArray("customEvents");
    for (int j = 0; j < schedulerState.templates[i].customEventsCount; j++) {
      JsonObject event = templateCustomEvents.createNestedObject();
      event["id"] = schedulerState.templates[i].customEvents[j].id;
      event["time"] = schedulerState.templates[i].customEvents[j].time;
      event["duration"] = schedulerState.templates[i].customEvents[j].duration;
      event["relay"] = schedulerState.templates[i].customEvents[j].relay;
    }
  }
  
  // Other state variables
  doc["isActive"] = schedulerState.isActive;
  doc["currentLightCondition"] = schedulerState.currentLightCondition;
  
  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    debugPrintln("DEBUG: Failed to write scheduler JSON to file");
  }
  
  file.close();
  debugPrintln("DEBUG: Scheduler state saved to file");
}

// Load scheduler state from file
void loadSchedulerState() {
  debugPrintln("DEBUG: Loading scheduler state from file");
  
  // Check if file exists
  if (!SPIFFS.exists(SCHEDULER_FILE)) {
    debugPrintln("DEBUG: Scheduler file not found, initializing with defaults");
    
    // Set defaults
    schedulerState.lightSchedule.lightsOnTime = "06:00";
    schedulerState.lightSchedule.lightsOffTime = "18:00";
    schedulerState.lightsOnSchedulesCount = 0;
    schedulerState.lightsOffSchedulesCount = 0;
    schedulerState.customEventsCount = 0;
    schedulerState.templatesCount = 0;
    schedulerState.isActive = false;
    schedulerState.currentLightCondition = "Unknown";
    schedulerState.hasNextEvent = false;
    
    // Save default state
    saveSchedulerState();
    return;
  }
  
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_READ);
  if (!file) {
    debugPrintln("DEBUG: Failed to open scheduler file for reading");
    return;
  }
  
  DynamicJsonDocument doc(16384); // Adjust size based on your needs
  
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    debugPrintf("DEBUG: Failed to parse scheduler JSON: %s\n", error.c_str());
    file.close();
    return;
  }
  
  file.close();
  
  // Clear current state
  memset(&schedulerState, 0, sizeof(schedulerState));
  
  // Light schedule
  if (doc.containsKey("lightSchedule")) {
    JsonObject lightSchedule = doc["lightSchedule"];
    schedulerState.lightSchedule.lightsOnTime = lightSchedule["lightsOnTime"].as<String>();
    schedulerState.lightSchedule.lightsOffTime = lightSchedule["lightsOffTime"].as<String>();
  } else {
    schedulerState.lightSchedule.lightsOnTime = "06:00";
    schedulerState.lightSchedule.lightsOffTime = "18:00";
  }
  
  // Lights On schedules
  if (doc.containsKey("lightsOnSchedules")) {
    JsonArray lightsOnSchedules = doc["lightsOnSchedules"];
    schedulerState.lightsOnSchedulesCount = min((int)lightsOnSchedules.size(), MAX_SCHEDULES);
    
    for (int i = 0; i < schedulerState.lightsOnSchedulesCount; i++) {
      JsonObject schedule = lightsOnSchedules[i];
      schedulerState.lightsOnSchedules[i].id = schedule["id"].as<String>();
      schedulerState.lightsOnSchedules[i].frequency = schedule["frequency"].as<int>();
      schedulerState.lightsOnSchedules[i].duration = schedule["duration"].as<int>();
      schedulerState.lightsOnSchedules[i].relay = schedule["relay"].as<int>();
    }
  }
  
// Lights Off schedules
  if (doc.containsKey("lightsOffSchedules")) {
    JsonArray lightsOffSchedules = doc["lightsOffSchedules"];
    schedulerState.lightsOffSchedulesCount = min((int)lightsOffSchedules.size(), MAX_SCHEDULES);
    
    for (int i = 0; i < schedulerState.lightsOffSchedulesCount; i++) {
      JsonObject schedule = lightsOffSchedules[i];
      schedulerState.lightsOffSchedules[i].id = schedule["id"].as<String>();
      schedulerState.lightsOffSchedules[i].frequency = schedule["frequency"].as<int>();
      schedulerState.lightsOffSchedules[i].duration = schedule["duration"].as<int>();
      schedulerState.lightsOffSchedules[i].relay = schedule["relay"].as<int>();
    }
  } else {
    schedulerState.lightsOffSchedulesCount = 0;
  }
  
  // Custom events
  if (doc.containsKey("customEvents")) {
    JsonArray customEvents = doc["customEvents"];
    schedulerState.customEventsCount = min((int)customEvents.size(), MAX_EVENTS);
    
    for (int i = 0; i < schedulerState.customEventsCount; i++) {
      JsonObject event = customEvents[i];
      schedulerState.customEvents[i].id = event["id"].as<String>();
      schedulerState.customEvents[i].time = event["time"].as<String>();
      schedulerState.customEvents[i].duration = event["duration"].as<int>();
      schedulerState.customEvents[i].relay = event["relay"].as<int>();
    }
  } else {
    schedulerState.customEventsCount = 0;
  }
  
  // Templates
  if (doc.containsKey("templates")) {
    JsonArray templates = doc["templates"];
    schedulerState.templatesCount = min((int)templates.size(), MAX_TEMPLATES);
    
    for (int i = 0; i < schedulerState.templatesCount; i++) {
      JsonObject templateObj = templates[i];
      schedulerState.templates[i].id = templateObj["id"].as<String>();
      schedulerState.templates[i].name = templateObj["name"].as<String>();
      
      if (templateObj.containsKey("lightSchedule")) {
        JsonObject templateLightSchedule = templateObj["lightSchedule"];
        schedulerState.templates[i].lightsOnTime = templateLightSchedule["lightsOnTime"].as<String>();
        schedulerState.templates[i].lightsOffTime = templateLightSchedule["lightsOffTime"].as<String>();
      } else {
        schedulerState.templates[i].lightsOnTime = schedulerState.lightSchedule.lightsOnTime;
        schedulerState.templates[i].lightsOffTime = schedulerState.lightSchedule.lightsOffTime;
      }
      
      // Template lights on schedules
      if (templateObj.containsKey("lightsOnSchedules")) {
        JsonArray templateLightsOnSchedules = templateObj["lightsOnSchedules"];
        schedulerState.templates[i].lightsOnSchedulesCount = min((int)templateLightsOnSchedules.size(), MAX_SCHEDULES);
        
        for (int j = 0; j < schedulerState.templates[i].lightsOnSchedulesCount; j++) {
          JsonObject schedule = templateLightsOnSchedules[j];
          schedulerState.templates[i].lightsOnSchedules[j].id = schedule["id"].as<String>();
          schedulerState.templates[i].lightsOnSchedules[j].frequency = schedule["frequency"].as<int>();
          schedulerState.templates[i].lightsOnSchedules[j].duration = schedule["duration"].as<int>();
          schedulerState.templates[i].lightsOnSchedules[j].relay = schedule["relay"].as<int>();
        }
      } else {
        schedulerState.templates[i].lightsOnSchedulesCount = 0;
      }
      
      // Template lights off schedules
      if (templateObj.containsKey("lightsOffSchedules")) {
        JsonArray templateLightsOffSchedules = templateObj["lightsOffSchedules"];
        schedulerState.templates[i].lightsOffSchedulesCount = min((int)templateLightsOffSchedules.size(), MAX_SCHEDULES);
        
        for (int j = 0; j < schedulerState.templates[i].lightsOffSchedulesCount; j++) {
          JsonObject schedule = templateLightsOffSchedules[j];
          schedulerState.templates[i].lightsOffSchedules[j].id = schedule["id"].as<String>();
          schedulerState.templates[i].lightsOffSchedules[j].frequency = schedule["frequency"].as<int>();
          schedulerState.templates[i].lightsOffSchedules[j].duration = schedule["duration"].as<int>();
          schedulerState.templates[i].lightsOffSchedules[j].relay = schedule["relay"].as<int>();
        }
      } else {
        schedulerState.templates[i].lightsOffSchedulesCount = 0;
      }
      
      // Template custom events
      if (templateObj.containsKey("customEvents")) {
        JsonArray templateCustomEvents = templateObj["customEvents"];
        schedulerState.templates[i].customEventsCount = min((int)templateCustomEvents.size(), MAX_EVENTS);
        
        for (int j = 0; j < schedulerState.templates[i].customEventsCount; j++) {
          JsonObject event = templateCustomEvents[j];
          schedulerState.templates[i].customEvents[j].id = event["id"].as<String>();
          schedulerState.templates[i].customEvents[j].time = event["time"].as<String>();
          schedulerState.templates[i].customEvents[j].duration = event["duration"].as<int>();
          schedulerState.templates[i].customEvents[j].relay = event["relay"].as<int>();
        }
      } else {
        schedulerState.templates[i].customEventsCount = 0;
      }
    }
  }
  
  // Other state variables
  schedulerState.isActive = doc.containsKey("isActive") ? doc["isActive"].as<bool>() : false;
  schedulerState.currentLightCondition = doc.containsKey("currentLightCondition") ? 
                                        doc["currentLightCondition"].as<String>() : "Unknown";
  
  // If the scheduler was active when the device was powered off, restart it
  if (schedulerState.isActive) {
    startSchedulerTask();
  }
  
  calculateNextEvent();
}

// Handler for loading scheduler state
void handleLoadSchedulerState(AsyncWebServerRequest *request) {
  debugPrintln("DEBUG: API request received: /api/scheduler/load");
  
  DynamicJsonDocument doc(16384);
  
  portENTER_CRITICAL(&schedulerMutex);
  
  // Light schedule
  JsonObject lightSchedule = doc.createNestedObject("lightSchedule");
  lightSchedule["lightsOnTime"] = schedulerState.lightSchedule.lightsOnTime;
  lightSchedule["lightsOffTime"] = schedulerState.lightSchedule.lightsOffTime;
  
  // Lights On schedules
  JsonArray lightsOnSchedules = doc.createNestedArray("lightsOnSchedules");
  for (int i = 0; i < schedulerState.lightsOnSchedulesCount; i++) {
    JsonObject schedule = lightsOnSchedules.createNestedObject();
    schedule["id"] = schedulerState.lightsOnSchedules[i].id;
    schedule["frequency"] = schedulerState.lightsOnSchedules[i].frequency;
    schedule["duration"] = schedulerState.lightsOnSchedules[i].duration;
    schedule["relay"] = schedulerState.lightsOnSchedules[i].relay;
  }
  
  // Lights Off schedules
  JsonArray lightsOffSchedules = doc.createNestedArray("lightsOffSchedules");
  for (int i = 0; i < schedulerState.lightsOffSchedulesCount; i++) {
    JsonObject schedule = lightsOffSchedules.createNestedObject();
    schedule["id"] = schedulerState.lightsOffSchedules[i].id;
    schedule["frequency"] = schedulerState.lightsOffSchedules[i].frequency;
    schedule["duration"] = schedulerState.lightsOffSchedules[i].duration;
    schedule["relay"] = schedulerState.lightsOffSchedules[i].relay;
  }
  
  // Custom events
  JsonArray customEvents = doc.createNestedArray("customEvents");
  for (int i = 0; i < schedulerState.customEventsCount; i++) {
    JsonObject event = customEvents.createNestedObject();
    event["id"] = schedulerState.customEvents[i].id;
    event["time"] = schedulerState.customEvents[i].time;
    event["duration"] = schedulerState.customEvents[i].duration;
    event["relay"] = schedulerState.customEvents[i].relay;
  }
  
  // Templates
  JsonArray templates = doc.createNestedArray("templates");
  for (int i = 0; i < schedulerState.templatesCount; i++) {
    JsonObject templateObj = templates.createNestedObject();
    templateObj["id"] = schedulerState.templates[i].id;
    templateObj["name"] = schedulerState.templates[i].name;
    
    JsonObject templateLightSchedule = templateObj.createNestedObject("lightSchedule");
    templateLightSchedule["lightsOnTime"] = schedulerState.templates[i].lightsOnTime;
    templateLightSchedule["lightsOffTime"] = schedulerState.templates[i].lightsOffTime;
    
    // Template lights on schedules
    JsonArray templateLightsOnSchedules = templateObj.createNestedArray("lightsOnSchedules");
    for (int j = 0; j < schedulerState.templates[i].lightsOnSchedulesCount; j++) {
      JsonObject schedule = templateLightsOnSchedules.createNestedObject();
      schedule["id"] = schedulerState.templates[i].lightsOnSchedules[j].id;
      schedule["frequency"] = schedulerState.templates[i].lightsOnSchedules[j].frequency;
      schedule["duration"] = schedulerState.templates[i].lightsOnSchedules[j].duration;
      schedule["relay"] = schedulerState.templates[i].lightsOnSchedules[j].relay;
    }
    
    // Template lights off schedules
    JsonArray templateLightsOffSchedules = templateObj.createNestedArray("lightsOffSchedules");
    for (int j = 0; j < schedulerState.templates[i].lightsOffSchedulesCount; j++) {
      JsonObject schedule = templateLightsOffSchedules.createNestedObject();
      schedule["id"] = schedulerState.templates[i].lightsOffSchedules[j].id;
      schedule["frequency"] = schedulerState.templates[i].lightsOffSchedules[j].frequency;
      schedule["duration"] = schedulerState.templates[i].lightsOffSchedules[j].duration;
      schedule["relay"] = schedulerState.templates[i].lightsOffSchedules[j].relay;
    }
    
    // Template custom events
    JsonArray templateCustomEvents = templateObj.createNestedArray("customEvents");
    for (int j = 0; j < schedulerState.templates[i].customEventsCount; j++) {
      JsonObject event = templateCustomEvents.createNestedObject();
      event["id"] = schedulerState.templates[i].customEvents[j].id;
      event["time"] = schedulerState.templates[i].customEvents[j].time;
      event["duration"] = schedulerState.templates[i].customEvents[j].duration;
      event["relay"] = schedulerState.templates[i].customEvents[j].relay;
    }
  }
  
  // Other state variables
  doc["isActive"] = schedulerState.isActive;
  doc["currentLightCondition"] = schedulerState.currentLightCondition;
  
  // Next event
  if (schedulerState.hasNextEvent) {
    JsonObject nextEvent = doc.createNestedObject("nextEvent");
    nextEvent["time"] = schedulerState.nextEvent.time;
    nextEvent["duration"] = schedulerState.nextEvent.duration;
    nextEvent["relay"] = schedulerState.nextEvent.relay;
  }
  
  portEXIT_CRITICAL(&schedulerMutex);
  
  String response;
  serializeJson(doc, response);
  
  request->send(200, "application/json", response);
}

// Handler for scheduler status
void handleSchedulerStatus(AsyncWebServerRequest *request) {
  debugPrintln("DEBUG: API request received: /api/scheduler/status");
  
  DynamicJsonDocument doc(1024);
  
  portENTER_CRITICAL(&schedulerMutex);
  
  doc["isActive"] = schedulerState.isActive;
  doc["lightCondition"] = schedulerState.currentLightCondition;
  
  // Next event
  if (schedulerState.hasNextEvent) {
    JsonObject nextEvent = doc.createNestedObject("nextEvent");
    nextEvent["time"] = schedulerState.nextEvent.time;
    nextEvent["duration"] = schedulerState.nextEvent.duration;
    nextEvent["relay"] = schedulerState.nextEvent.relay;
  }
  
  portEXIT_CRITICAL(&schedulerMutex);
  
  String response;
  serializeJson(doc, response);
  
  request->send(200, "application/json", response);
}

// Handler for activating scheduler
void handleActivateScheduler(AsyncWebServerRequest *request) {
  debugPrintln("DEBUG: API request received: /api/scheduler/activate");
  
  portENTER_CRITICAL(&schedulerMutex);
  bool wasActive = schedulerState.isActive;
  portEXIT_CRITICAL(&schedulerMutex);
  
  if (wasActive) {
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler already active\"}");
    return;
  }
  
  startSchedulerTask();
  
  // Save state to make sure active state is persistent
  saveSchedulerState();
  
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler activated\"}");
}

// Handler for deactivating scheduler
void handleDeactivateScheduler(AsyncWebServerRequest *request) {
  debugPrintln("DEBUG: API request received: /api/scheduler/deactivate");
  
  portENTER_CRITICAL(&schedulerMutex);
  bool wasActive = schedulerState.isActive;
  portEXIT_CRITICAL(&schedulerMutex);
  
  if (!wasActive) {
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler already inactive\"}");
    return;
  }
  
  stopSchedulerTask();
  
  // Save state to make sure inactive state is persistent
  saveSchedulerState();
  
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler deactivated\"}");
}

// Handler for manual watering
void handleManualWatering(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("DEBUG: API request received: /api/relay/manual");
  
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  if (!doc.containsKey("relay") || !doc.containsKey("duration")) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing relay or duration\"}");
    return;
  }
  
  int relay = doc["relay"].as<int>();
  int duration = doc["duration"].as<int>();
  
  // Validate inputs
  if (relay < 0 || relay > 7) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid relay number\"}");
    return;
  }
  
  if (duration < 5 || duration > 300) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid duration\"}");
    return;
  }
  
  // Send success response before starting watering
  // (because watering will block)
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Manual watering started\"}");

  // Execute watering in a new task to avoid blocking the web server
  xTaskCreate(
    [](void* parameter) {
      ManualWateringParams* params = (ManualWateringParams*)parameter;
      executeWatering(params->relay, params->duration);
      delete params;
      vTaskDelete(NULL);
    },
    "ManualWateringTask",
    2048,
    new ManualWateringParams{relay, duration},
    1,
    NULL
  );
}

  // Initialize time for the scheduler
  void initSchedulerTime() {
  debugPrintln("DEBUG: Initializing time for scheduler...");
  
  // Configure time zone and NTP servers
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  debugPrintln("DEBUG: Waiting for time to be set...");
  int retry = 0;
  const int maxRetries = 1;
  struct tm timeinfo;
  
  while(!getLocalTime(&timeinfo) && retry < maxRetries) {
    debugPrintln("DEBUG: Failed to obtain time, retrying...");
    retry++;
    delay(1000);
  }
  
  if (retry >= maxRetries) {
    debugPrintln("DEBUG: Failed to set time after maximum retries");
  } else {
    debugPrintf("DEBUG: Current time: %02d:%02d:%02d\n", 
               timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }
}

// Initialize the scheduler
void initScheduler() {
  debugPrintln("DEBUG: Initializing scheduler...");
  
  // Initialize time
  initSchedulerTime();
  
  // Load scheduler state from file
  loadSchedulerState();
  
  // Setup API routes
  server.on("/scheduler.html", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving scheduler.html");
    request->send(SPIFFS, "/scheduler.html", String(), false);
  });
  
  server.on("/css/scheduler.css", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving scheduler.css");
    request->send(SPIFFS, "/css/scheduler.css", "text/css");
  });
  
  server.on("/js/scheduler.js", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving scheduler.js");
    request->send(SPIFFS, "/js/scheduler.js", "text/javascript");
  });
  
  // API endpoints
  server.on("/api/scheduler/save", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleSaveSchedulerState
  );
  
  server.on("/api/wifi/config", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleSetWiFiCredentials
  );

  server.on("/api/scheduler/load", HTTP_GET, handleLoadSchedulerState);
  
  server.on("/api/scheduler/status", HTTP_GET, handleSchedulerStatus);
  
  server.on("/api/scheduler/activate", HTTP_POST, handleActivateScheduler);
  
  server.on("/api/scheduler/deactivate", HTTP_POST, handleDeactivateScheduler);
  
  server.on("/api/relay/manual", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleManualWatering
  );
  
  debugPrintln("DEBUG: Scheduler initialized");
}

// CRC16 calculation for MODBUS
uint16_t calculateCRC16(uint8_t* buffer, uint8_t length) {
  uint16_t crc = 0xFFFF;
  
  for (uint8_t i = 0; i < length; i++) {
    crc ^= buffer[i];
    
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  
  return crc;
}

// Send MODBUS request and get response
bool sendModbusRequest(uint8_t* request, uint8_t requestLength, uint8_t* response, uint8_t& responseLength) {
  debugPrintln("DEBUG: Sending MODBUS request...");
  
  // Print request bytes for debugging
  debugPrint("DEBUG: Request bytes: ");
  for (int i = 0; i < requestLength; i++) {
    debugPrintf("%02X ", request[i]);
  }
  debugPrintln("");
  
  // Switch to RS485 mode
  switchToRS485Mode();
  
  // Clear any pending data
  while (Serial.available()) {
    Serial.read();
  }
  
  // Enable transmitter
  rs485Transmit(true);
  
  // Send request
  Serial.write(request, requestLength);
  Serial.flush();
  
  // Switch to receive mode
  rs485Transmit(false);
  
  // Wait for response with timeout
  uint32_t startTime = millis();
  uint32_t waitTime = 0;
  bool responseReceived = false;
  
  while (waitTime < 1000) {
    if (Serial.available() >= 5) {
        responseReceived = true;
        break;
      }
      delay(10);
      waitTime = millis() - startTime;
    }
    
    // Read response
    responseLength = 0;
    while (Serial.available() && responseLength < MODBUS_BUFFER_SIZE) {
      response[responseLength++] = Serial.read();
    }
    
    // Switch back to debug mode
    switchToDebugMode();
    
    debugPrintf("DEBUG: Received %d bytes\n", responseLength);
    
    // Print response bytes for debugging
    if (responseLength > 0) {
      debugPrint("DEBUG: Response bytes: ");
      for (int i = 0; i < responseLength; i++) {
        debugPrintf("%02X ", response[i]);
      }
      debugPrintln("");
    }
    
    // Check if we received a valid response
    if (!responseReceived || responseLength < 5) {
      debugPrintln("DEBUG: Response too short or timed out");
      return false; // Timeout or invalid response
    }
    
    // Validate CRC
    uint16_t receivedCRC = (response[responseLength - 1] << 8) | response[responseLength - 2];
    uint16_t calculatedCRC = calculateCRC16(response, responseLength - 2);
    
    debugPrintf("DEBUG: CRC check - Received: 0x%04X, Calculated: 0x%04X\n", receivedCRC, calculatedCRC);
    
    return (receivedCRC == calculatedCRC);
  }
  
  // Task to update analog values
  void vAnalogTask(void *pvParameters) {
    debugPrintln("DEBUG: Analog task started");
    
    for (;;) {
      // Read analog inputs (similar to sensorTestLoop in TestMode.cpp)
      int analog_value[8];
      
      // Safely read analog inputs with error handling
      try {
        analog_value[0] = analogRead(AI_V1);
      } catch (...) {
        analog_value[0] = 0;
      }
      
      try {
        analog_value[1] = analogRead(AI_V2);
      } catch (...) {
        analog_value[1] = 0;
      }
      
      try {
        analog_value[2] = analogRead(AI_V3);
      } catch (...) {
        analog_value[2] = 0;
      }
      
      try {
        analog_value[3] = analogRead(AI_V4);
      } catch (...) {
        analog_value[3] = 0;
      }
      
      try {
        analog_value[4] = analogRead(AI_I1);
      } catch (...) {
        analog_value[4] = 0;
      }
      
      try {
        analog_value[5] = analogRead(AI_I2);
      } catch (...) {
        analog_value[5] = 0;
      }
      
      try {
        analog_value[6] = analogRead(AI_I3);
      } catch (...) {
        analog_value[6] = 0;
      }
      
      try {
        analog_value[7] = analogRead(AI_I4);
      } catch (...) {
        analog_value[7] = 0;
      }
      
      // Convert to voltage (same formula as in TestMode.cpp)
      voltageValues[0] = (float)analog_value[0] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
      voltageValues[1] = (float)analog_value[1] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
      voltageValues[2] = (float)analog_value[2] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
      voltageValues[3] = (float)analog_value[3] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
      
      // Convert to current (same formula as in TestMode.cpp)
      currentValues[0] = ((float)analog_value[4] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
      currentValues[1] = ((float)analog_value[5] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
      currentValues[2] = ((float)analog_value[6] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
      currentValues[3] = ((float)analog_value[7] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
      
      // Read button states - with error handling
      try {
        buttonStates[0] = (digitalRead(BTN1) == LOW);
        buttonStates[1] = (digitalRead(BTN2) == LOW);
        buttonStates[2] = (digitalRead(BTN3) == LOW);
        buttonStates[3] = (digitalRead(BTN4) == LOW);
      } catch (...) {
        // Keep previous button states on error
      }
      
      // Read input states from 74HC165 - only if initialization completed
      if (initTestModeComplete) {
        try {
          uint8_t diStatus = Get_DI_Value();
          for (int i = 0; i < 8; i++) {
            inputStates[i] = (diStatus & (1 << i)) != 0;
          }
        } catch (...) {
          // Keep previous input states on error
        }
      }
      
      // Print debug every 5 seconds
      static uint32_t lastDebugTime = 0;
      if (millis() - lastDebugTime > 5000) {
        lastDebugTime = millis();
        debugPrintln("DEBUG: Analog readings update...");
        debugPrintf("V1=%.2fV, V2=%.2fV, V3=%.2fV, V4=%.2fV\n", 
                    voltageValues[0], voltageValues[1], voltageValues[2], voltageValues[3]);
        debugPrintf("I1=%.2fmA, I2=%.2fmA, I3=%.2fmA, I4=%.2fmA\n", 
                    currentValues[0], currentValues[1], currentValues[2], currentValues[3]);
        debugPrintf("BTN: %d %d %d %d, Relay state: 0x%02X\n", 
                    buttonStates[0], buttonStates[1], buttonStates[2], buttonStates[3], relayState);
      }
      
      vTaskDelay(pdMS_TO_TICKS(200)); // Update every 200ms
    }
  }
  
  // Handler for IO status
  void handleGetIOStatus(AsyncWebServerRequest *request) {
    debugPrintln("DEBUG: API request received: /api/io/status");
    
    // Debug print analog values before creating response
    debugPrintln("DEBUG: Analog values being sent:");
    for (int i = 0; i < 4; i++) {
      debugPrintf("DEBUG: V%d=%.2fV, I%d=%.2fmA\n", 
                i+1, voltageValues[i], i+1, currentValues[i]);
    }
    
    DynamicJsonDocument doc(2048); // Increased size to ensure enough space
    
    // Add relay states
    JsonArray relays = doc.createNestedArray("relays");
    for (int i = 0; i < 8; i++) {
      JsonObject relay = relays.createNestedObject();
      relay["id"] = i;
      relay["state"] = (relayState & (1 << i)) != 0;
    }
    
    // Add button states
    JsonArray buttons = doc.createNestedArray("buttons");
    for (int i = 0; i < 4; i++) {
      JsonObject button = buttons.createNestedObject();
      button["id"] = i;
      button["state"] = buttonStates[i];
    }
    
    // Add input states
    JsonArray inputs = doc.createNestedArray("inputs");
    for (int i = 0; i < 8; i++) {
      JsonObject input = inputs.createNestedObject();
      input["id"] = i;
      input["state"] = inputStates[i];
    }
    
    // Add voltage inputs - FIX: explicitly create array first, then add objects
    JsonArray voltageInputsArray = doc.createNestedArray("voltageInputs");
    for (int i = 0; i < 4; i++) {
      JsonObject input = voltageInputsArray.createNestedObject();
      input["id"] = i;
      input["value"] = voltageValues[i];
    }
    
    // Add current inputs - FIX: explicitly create array first, then add objects
    JsonArray currentInputsArray = doc.createNestedArray("currentInputs");
    for (int i = 0; i < 4; i++) {
      JsonObject input = currentInputsArray.createNestedObject();
      input["id"] = i;
      input["value"] = currentValues[i];
    }
    
    // Debug print the final JSON size
    debugPrintf("DEBUG: JSON document size: %d bytes\n", doc.memoryUsage());
    
    String response;
    serializeJson(doc, response);
    
    // Debug print a sample of the response - FIX: use c_str() for String conversion
    debugPrint("DEBUG: JSON response sample: ");
    if (response.length() > 100) {
      String sample = response.substring(0, 100) + "...";
      debugPrintln(sample.c_str());
    } else {
      debugPrintln(response.c_str());
    }
    
    request->send(200, "application/json", response);
    debugPrintln("DEBUG: IO status sent to client");
  }
  
  // Handler for setting relay states
  void handleSetRelay(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    debugPrintln("DEBUG: API request received: /api/io/relay");
    
    // Detailed debug of received data
    debugPrintf("DEBUG: Received %d bytes of data\n", len);
    
    // Check if we received any data
    if (len == 0) {
      debugPrintln("DEBUG: No data received");
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data received\"}");
      return;
    }
    
    // For debugging - print raw data
    debugPrint("DEBUG: Raw request data: ");
    for (size_t i = 0; i < min(len, size_t(20)); i++) {
      debugPrintf("%02X ", data[i]);
    }
    if (len > 20) {
      debugPrint("...");
    }
    debugPrintln("");
    
    // For debugging - try to print as string
    String dataStr = "";
    for (size_t i = 0; i < min(len, size_t(100)); i++) {
      dataStr += (char)data[i];
    }
    debugPrintf("DEBUG: Request data as string: %s\n", dataStr.c_str());
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
      debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
      return;
    }
    
    // Detailed debug of parsed JSON
    String jsonDump;
    serializeJson(doc, jsonDump);
    debugPrintf("DEBUG: Parsed JSON: %s\n", jsonDump.c_str());
    
    if (doc.containsKey("relay") && doc.containsKey("state")) {
      int relay = doc["relay"].as<int>();
      bool state = doc["state"].as<bool>();
      
      debugPrintf("DEBUG: Setting relay %d to %s\n", relay, state ? "ON" : "OFF");
      
      if (relay >= 0 && relay < 8) {
        uint8_t oldState = relayState;
        
        if (state) {
          relayState |= (1 << relay);
        } else {
          relayState &= ~(1 << relay);
        }
        
        debugPrintf("DEBUG: Relay state changed: 0x%02X -> 0x%02X\n", oldState, relayState);
        
        // Create a more helpful response
        String responseJson = "{\"status\":\"success\",\"relay\":" + String(relay) + 
                             ",\"state\":" + (state ? "true" : "false") + 
                             ",\"relayState\":\"0x" + String(relayState, HEX) + "\"}";
        
        request->send(200, "application/json", responseJson);
        return;
      } else {
        debugPrintln("DEBUG: Invalid relay ID");
      }
    } else {
      debugPrintln("DEBUG: Missing relay ID or state");
      // Check what keys are actually present
      String keys = "Keys present:";
      for (JsonPair kv : doc.as<JsonObject>()) {
        keys += " ";
        keys += kv.key().c_str();
      }
      debugPrintln(keys.c_str());
    }
    
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid relay ID or state\"}");
  }
  
  // Handler for setting all relay states
  void handleSetAllRelays(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    debugPrintln("DEBUG: API request received: /api/io/relays");
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
      debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
      return;
    }
    
    if (doc.containsKey("states")) {
      JsonArray states = doc["states"].as<JsonArray>();
      if (states.size() == 8) {
        debugPrintln("DEBUG: Setting all relays");
        relayState = 0;
        for (int i = 0; i < 8; i++) {
          if (states[i].as<bool>()) {
            relayState |= (1 << i);
          }
        }
        
        debugPrintf("DEBUG: New relay state: 0x%02X\n", relayState);
        request->send(200, "application/json", "{\"status\":\"success\"}");
        return;
      } else {
        debugPrintf("DEBUG: Expected 8 states, got %d\n", states.size());
      }
    } else {
      debugPrintln("DEBUG: Missing states array");
    }
    
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid relay states\"}");
  }
  
  // This task will cycle through all relays for testing
  void vRelayTestTask(void *pvParameters) {
    debugPrintln("DEBUG: Relay test task started");
    
    // First turn all relays off
    relayState = 0x00;
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Turn each relay on and off in sequence
    for (int i = 0; i < 8; i++) {
      debugPrintf("DEBUG: Testing relay %d - ON\n", i + 1);
      
      // Turn on this relay
      relayState = (1 << i);
      vTaskDelay(pdMS_TO_TICKS(500));
      
      debugPrintf("DEBUG: Testing relay %d - OFF\n", i + 1);
      
      // Turn off this relay
      relayState = 0x00;
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Turn all relays on
    debugPrintln("DEBUG: All relays ON");
    relayState = 0xFF;
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Turn all relays off
    debugPrintln("DEBUG: All relays OFF");
    relayState = 0x00;
    vTaskDelay(pdMS_TO_TICKS(500));
    
    debugPrintln("DEBUG: Relay test completed");
    
    // Delete this task when finished
    vTaskDelete(NULL);
  }
  
  // Handler for MODBUS requests
  void handleModbusRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    debugPrintln("DEBUG: API request received: /api/modbus/request");
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
      debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
      return;
    }
    
    uint8_t deviceAddr, functionCode;
    uint16_t startAddr, quantity;
    bool success = false;
    
    if (doc.containsKey("deviceAddr") && doc.containsKey("functionCode") && 
        doc.containsKey("startAddr")) {
        
      deviceAddr = doc["deviceAddr"].as<uint8_t>();
      functionCode = doc["functionCode"].as<uint8_t>();
      startAddr = doc["startAddr"].as<uint16_t>();
      
      debugPrintf("DEBUG: MODBUS request - Device: %d, Function: %d, Start Address: %d\n", 
                  deviceAddr, functionCode, startAddr);
      
      // Prepare MODBUS request
      uint8_t requestLength = 0;
      modbusRequestBuffer[requestLength++] = deviceAddr;
      modbusRequestBuffer[requestLength++] = functionCode;
      modbusRequestBuffer[requestLength++] = highByte(startAddr);
      modbusRequestBuffer[requestLength++] = lowByte(startAddr);
      
      // Different handling based on function code
      switch (functionCode) {
        case 0x01: // Read Coils
        case 0x02: // Read Discrete Inputs
        case 0x03: // Read Holding Registers
        case 0x04: // Read Input Registers
          if (doc.containsKey("quantity")) {
            quantity = doc["quantity"].as<uint16_t>();
            debugPrintf("DEBUG: Read request with quantity: %d\n", quantity);
            modbusRequestBuffer[requestLength++] = highByte(quantity);
            modbusRequestBuffer[requestLength++] = lowByte(quantity);
          } else {
            debugPrintln("DEBUG: Missing quantity parameter");
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing quantity parameter\"}");
            return;
          }
          break;
          
        case 0x05: // Write Single Coil
          if (doc.containsKey("value")) {
            bool value = doc["value"].as<bool>();
            debugPrintf("DEBUG: Write single coil with value: %d\n", value);
            modbusRequestBuffer[requestLength++] = value ? 0xFF : 0x00;
            modbusRequestBuffer[requestLength++] = 0x00;
          } else {
            debugPrintln("DEBUG: Missing value parameter");
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing value parameter\"}");
            return;
          }
          break;
          
        case 0x06: // Write Single Register
          if (doc.containsKey("value")) {
            uint16_t value = doc["value"].as<uint16_t>();
            debugPrintf("DEBUG: Write single register with value: %d\n", value);
            modbusRequestBuffer[requestLength++] = highByte(value);
            modbusRequestBuffer[requestLength++] = lowByte(value);
          } else {
            debugPrintln("DEBUG: Missing value parameter");
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing value parameter\"}");
            return;
          }
          break;
          
        case 0x0F: // Write Multiple Coils
        case 0x10: // Write Multiple Registers
          if (doc.containsKey("values")) {
            JsonArray values = doc["values"].as<JsonArray>();
            quantity = values.size();
            
            debugPrintf("DEBUG: Write multiple with %d values\n", quantity);
            
            modbusRequestBuffer[requestLength++] = highByte(quantity);
            modbusRequestBuffer[requestLength++] = lowByte(quantity);
            
            if (functionCode == 0x0F) { // Write Multiple Coils
              uint8_t byteCount = (quantity + 7) / 8;
              modbusRequestBuffer[requestLength++] = byteCount;
              
              uint8_t byteValue = 0;
              uint8_t bitPosition = 0;
              
              for (size_t i = 0; i < quantity; i++) {
                if (values[i].as<bool>()) {
                  byteValue |= (1 << bitPosition);
                }
                
                bitPosition++;
                if (bitPosition == 8 || i == quantity - 1) {
                  modbusRequestBuffer[requestLength++] = byteValue;
                  byteValue = 0;
                  bitPosition = 0;
                }
              }
            } else { // Write Multiple Registers
              uint8_t byteCount = quantity * 2;
              modbusRequestBuffer[requestLength++] = byteCount;
              
              for (size_t i = 0; i < quantity; i++) {
                uint16_t value = values[i].as<uint16_t>();
                modbusRequestBuffer[requestLength++] = highByte(value);
                modbusRequestBuffer[requestLength++] = lowByte(value);
              }
            }
          } else {
            debugPrintln("DEBUG: Missing values parameter");
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing values parameter\"}");
            return;
          }
          break;
          
        default:
          debugPrintf("DEBUG: Unsupported function code: %d\n", functionCode);
          request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Unsupported function code\"}");
          return;
      }
      
      // Calculate CRC and add to request
      uint16_t crc = calculateCRC16(modbusRequestBuffer, requestLength);
      modbusRequestBuffer[requestLength++] = lowByte(crc);
      modbusRequestBuffer[requestLength++] = highByte(crc);
      
      // Send MODBUS request and get response
      uint8_t responseLength = 0;
      success = sendModbusRequest(modbusRequestBuffer, requestLength, modbusResponseBuffer, responseLength);
      
      // Prepare response JSON
      DynamicJsonDocument responseDoc(1024);
      responseDoc["success"] = success;
      responseDoc["functionCode"] = functionCode;
      
      if (success) {
        debugPrintln("DEBUG: MODBUS request successful");
        JsonArray data = responseDoc.createNestedArray("data");
        
        // Parse response based on function code
        if (functionCode == 0x01 || functionCode == 0x02) {
          // Read Coils or Discrete Inputs
          uint8_t byteCount = modbusResponseBuffer[2];
          for (uint8_t i = 0; i < byteCount; i++) {
            uint8_t coilByte = modbusResponseBuffer[3 + i];
            for (uint8_t bit = 0; bit < 8; bit++) {
              if (data.size() < quantity) {
                data.add((coilByte & (1 << bit)) != 0);
              }
            }
          }
        } else if (functionCode == 0x03 || functionCode == 0x04) {
          // Read Holding Registers or Input Registers
          uint8_t byteCount = modbusResponseBuffer[2];
          for (uint8_t i = 0; i < byteCount; i += 2) {
            uint16_t regValue = (modbusResponseBuffer[3 + i] << 8) | modbusResponseBuffer[4 + i];
            data.add(regValue);
          }
        } else if (functionCode == 0x05 || functionCode == 0x06) {
          // Write Single Coil or Register
          uint16_t address = (modbusResponseBuffer[2] << 8) | modbusResponseBuffer[3];
          uint16_t value = (modbusResponseBuffer[4] << 8) | modbusResponseBuffer[5];
          data.add(address);
          data.add(value);
        } else if (functionCode == 0x0F || functionCode == 0x10) {
          // Write Multiple Coils or Registers
          uint16_t startAddress = (modbusResponseBuffer[2] << 8) | modbusResponseBuffer[3];
          uint16_t quantity = (modbusResponseBuffer[4] << 8) | modbusResponseBuffer[5];
          data.add(startAddress);
          data.add(quantity);
        }
      } else {
        debugPrintln("DEBUG: MODBUS communication failed");
        responseDoc["error"] = "MODBUS communication failed";
      }
      
      String responseJson;
      serializeJson(responseDoc, responseJson);
      request->send(200, "application/json", responseJson);
      
    } else {
      debugPrintln("DEBUG: Missing required parameters");
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameters\"}");
    }
  }
  
  void setup() {
    // Initialize serial in debug mode 
    Serial.begin(115200);
    currentUartMode = UART_MODE_DEBUG;
    delay(500); // Give serial time to initialize
    
    // Disabling Loop Watchdogs
    disableLoopWDT(); // Disable the main loop watchdog
    disableCore0WDT(); // Disable CPU0 watchdog
    disableCore1WDT(); // Disable CPU1 watchdog

    debugPrintln("\n\n-------------------------");
    debugPrintln("ES32A08 Setup Utility Starting...");
    debugPrintln("-------------------------");
  
    // Initialize SPIFFS
    debugPrintln("DEBUG: Initializing SPIFFS...");
    if(!SPIFFS.begin(true)){
      debugPrintln("ERROR: SPIFFS initialization failed!");
      return;
    }
    debugPrintln("DEBUG: SPIFFS initialized successfully");
    
    // List available files
    debugPrintln("DEBUG: SPIFFS files:");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while(file){
      debugPrint("  - ");
      debugPrint(file.name());
      debugPrint(" (");
      char sizeBuf[16];
      sprintf(sizeBuf, "%d", file.size());
      debugPrint(sizeBuf);
      debugPrintln(" bytes)");
      file = root.openNextFile();
    }
  
    // Setup dual WiFi mode
    setupDualWiFi();

    // Add this after initializing SPIFFS and before starting the web server
    initScheduler();

    // Initialize WiFi Routes
    initWiFiRoutes();  

    // Initialize all pins
    debugPrintln("DEBUG: Initializing pins...");
    pinMode(BTN1, INPUT_PULLUP);
    pinMode(BTN2, INPUT_PULLUP);
    pinMode(BTN3, INPUT_PULLUP);
    pinMode(BTN4, INPUT_PULLUP);
    pinMode(PWR_LED, OUTPUT);
    digitalWrite(PWR_LED, HIGH);
    debugPrintln("DEBUG: Button pins initialized");
    
    // Initialize the 74HC165 (digital inputs)
    pinMode(LOAD_165, OUTPUT);
    pinMode(CLK_165, OUTPUT);
    pinMode(DATA165, INPUT);
    debugPrintln("DEBUG: 74HC165 pins initialized");
    
    // Initialize RS485 direction pin
    pinMode(RS485_DE, OUTPUT);
    digitalWrite(RS485_DE, LOW); // Receive mode
    rs485Initialized = true;
    
    // Initialize the shift register (74HC595) with timeout
    debugPrintln("DEBUG: Starting 74HC595 initialization with timeout...");
    initTestModeComplete = false;
    xTaskCreatePinnedToCore(initTestModeTask, "InitTask", 4096, NULL, 1, NULL,1);
    
    // Wait for initialization or timeout
    unsigned long startTime = millis();
    while (!initTestModeComplete && (millis() - startTime < 3000)) {
      delay(100);
      debugPrint(".");
    }
    
    if (initTestModeComplete) {
      debugPrintln("\nDEBUG: 74HC595 initialization completed");
    } else {
      debugPrintln("\nDEBUG: 74HC595 initialization timed out, continuing anyway");
    }
    
    // Try to initialize RS485 once
    debugPrintln("DEBUG: Testing RS485 communication...");
    // Switch UART temporarily to RS485 mode to test
    switchToRS485Mode();
    
    // Simple test of RS485
    rs485Transmit(true);
    Serial.write(0xFF); // Send a test byte
    Serial.flush();
    rs485Transmit(false);
    
    // Wait briefly for any response
    delay(100);
    
    // Switch back to debug mode
    switchToDebugMode();
    debugPrintln("DEBUG: RS485 test completed");
    
    // Add this initialization to your setup() function in main.cpp,
    // right after initializing other pins and before starting the web server
  
    // Initialize analog values to zero
    for (int i = 0; i < 4; i++) {
      voltageValues[i] = 0.0;
      currentValues[i] = 0.0;
    }
  
    debugPrintln("DEBUG: Initialized analog value arrays");
  
    // Perform a first reading of analog values
    int analog_value[8] = {0};
    try {
      analog_value[0] = analogRead(AI_V1);
      analog_value[1] = analogRead(AI_V2);
      analog_value[2] = analogRead(AI_V3);
      analog_value[3] = analogRead(AI_V4);
      analog_value[4] = analogRead(AI_I1);
      analog_value[5] = analogRead(AI_I2);
      analog_value[6] = analogRead(AI_I3);
      analog_value[7] = analogRead(AI_I4);
      
      // Convert to voltage
      voltageValues[0] = (float)analog_value[0] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
      voltageValues[1] = (float)analog_value[1] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
    voltageValues[2] = (float)analog_value[2] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
    voltageValues[3] = (float)analog_value[3] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
    
    // Convert to current
    currentValues[0] = ((float)analog_value[4] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
    currentValues[1] = ((float)analog_value[5] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
    currentValues[2] = ((float)analog_value[6] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
    currentValues[3] = ((float)analog_value[7] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
  } catch (...) {
    debugPrintln("DEBUG: Error during initial analog read");
  }

  debugPrintln("DEBUG: Initial analog values:");
  for (int i = 0; i < 4; i++) {
    debugPrintf("DEBUG: V%d=%.2fV, I%d=%.2fmA\n", 
              i+1, voltageValues[i], i+1, currentValues[i]);
  }

  // Set up Access Point
  debugPrintln("DEBUG: Setting up WiFi Access Point...");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  debugPrint("DEBUG: AP IP address: ");
  debugPrintln(IP.toString().c_str());

  // Route for root / web page
  debugPrintln("DEBUG: Setting up web server routes...");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving index.html");
    request->send(SPIFFS, "/index.html", String(), false);
  });
  
  // Route for MODBUS RTU tester page
  server.on("/modbus.html", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving modbus.html");
    request->send(SPIFFS, "/modbus.html", String(), false);
  });
  
  // Routes for static files
  server.on("/css/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving style.css");
    request->send(SPIFFS, "/css/style.css", "text/css");
  });
  
  server.on("/js/dashboard.js", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving dashboard.js");
    request->send(SPIFFS, "/js/dashboard.js", "text/javascript");
  });
  
  server.on("/js/modbus.js", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving modbus.js");
    request->send(SPIFFS, "/js/modbus.js", "text/javascript");
  });
  
  // API endpoints
  debugPrintln("DEBUG: Setting up API endpoints...");
  server.on("/api/io/status", HTTP_GET, handleGetIOStatus);
  
  server.on("/api/io/relay", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleSetRelay
  );
  
  server.on("/api/io/relays", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleSetAllRelays
  );
  
  server.on("/api/modbus/request", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleModbusRequest
  );
  
  // Create task to handle relay outputs continuously
  debugPrintln("DEBUG: Creating relay update task...");
  xTaskCreatePinnedToCore(
    vRelayUpdateTask,    // Function that implements the task
    "RelayTask",         // Text name for the task
    2048,                // Stack size in bytes
    NULL,                // Parameter passed into the task
    1,                   // Priority of the task
    NULL,                // Task handle
    1                    //Pinned to Core 1
  );
  debugPrintln("DEBUG: Relay update task created");
  
// Add a not found handler
  server.onNotFound([](AsyncWebServerRequest *request) {
    String message = "DEBUG: Not found: " + request->url();
    debugPrintln(message.c_str());  // Convert String to const char*
    request->send(404, "text/plain", "Not found");
  });

  // Set a catch-all handler for any request that doesn't match a route
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("DEBUG: Request body received");
  });

  // Add watchdog reset task
  xTaskCreate(
    [](void *parameter) {
      for(;;) {
        // Feed the watchdog
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    },
    "WatchdogTask",
    2048,
    NULL,
    1,
    NULL
  );

  // When starting the server, add debug information
  debugPrintln("DEBUG: Starting web server...");
  server.begin();
  debugPrintln("DEBUG: Web server started");
  
  // Create task to update analog values
  debugPrintln("DEBUG: Creating analog update task...");
  xTaskCreatePinnedToCore(vAnalogTask, "AnalogTask", 4096, NULL, 1, NULL,1);
  
  debugPrintln("DEBUG: Setup complete!");
  debugPrintln("-------------------------");
  debugPrintf("Connect to WiFi SSID: %s with password: %s\n", ssid, password);
  debugPrintf("Then navigate to http://%s in your browser\n", IP.toString().c_str());
  debugPrintln("-------------------------");
}

void loop() {
  // Nothing to do here since we're using FreeRTOS tasks
  static uint32_t heartbeatTime = 0;
  if (millis() - heartbeatTime > 10000) {
    heartbeatTime = millis();
    
    // Make sure we're in debug mode for heartbeat messages
    switchToDebugMode();
    
    debugPrintln("DEBUG: Heartbeat - ESP32 still running");
    
    // Print memory stats
    debugPrintf("DEBUG: Free heap: %d bytes\n", ESP.getFreeHeap());
    debugPrintf("DEBUG: Minimum free heap: %d bytes\n", ESP.getMinFreeHeap());
    
    // Print WiFi stats
    debugPrintf("DEBUG: WiFi AP status: %d, Stations connected: %d\n", 
               WiFi.status(), WiFi.softAPgetStationNum());
  }
  
  vTaskDelay(pdMS_TO_TICKS(1000)); // Prevent watchdog issues
}