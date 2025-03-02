#include "Scheduler.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include "IOManager.h"
#include <ESPAsyncWebServer.h>

// Reference to the web server defined elsewhere in the project
extern AsyncWebServer server;

// Debug macros
#ifndef debugPrintln
  #define debugPrintln(msg) Serial.println(msg)
#endif
#ifndef debugPrintf
  #define debugPrintf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#endif

// Global scheduler state
SchedulerState schedulerState;
static TaskHandle_t schedulerTaskHandle = NULL;
static bool schedulerActive = false;
static time_t lastEventCheckTime = 0;

// Global WebSocket objects
AsyncWebSocket schedulerWs("/scheduler-ws");
EditSession currentSession;
unsigned long lastTimeoutCheck = 0;

// Forward declarations
void checkAndExecuteScheduledEvents();
void handleWebSocketEvent(AsyncWebSocket* webSocket, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);
void handleWebSocketMessage(AsyncWebSocket* webSocket, AsyncWebSocketClient* client, AwsFrameInfo* info, uint8_t* data, size_t len);
JsonObject serializeSchedule(Schedule& schedule, bool convertToLocalTime);
void sendSchedulerState(AsyncWebSocketClient* client);
void resetSession();
String generateSessionId();
void sendErrorResponse(AsyncWebSocketClient* client, const String& message);
void broadcastSchedulerUpdate();
void checkSchedulerTimeouts();
void updateSchedulerWebSocket();

// Convert times between local and UTC formats
String localTimeToUTC(const String& localTime) {
  int hours, minutes;
  sscanf(localTime.c_str(), "%d:%d", &hours, &minutes);
  
  time_t now = time(NULL);
  struct tm localTimeInfo;
  localtime_r(&now, &localTimeInfo);
  
  // Create a tm structure with today's date but the specified time
  struct tm eventTimeLocal = localTimeInfo;
  eventTimeLocal.tm_hour = hours;
  eventTimeLocal.tm_min = minutes;
  eventTimeLocal.tm_sec = 0;
  
  // Convert to UTC
  time_t eventTimeSeconds = mktime(&eventTimeLocal);
  struct tm utcTimeInfo;
  gmtime_r(&eventTimeSeconds, &utcTimeInfo);
  
  char buffer[6]; // HH:MM\0
  sprintf(buffer, "%02d:%02d", utcTimeInfo.tm_hour, utcTimeInfo.tm_min);
  return String(buffer);
}

String utcToLocalTime(const String& utcTime) {
  int hours, minutes;
  sscanf(utcTime.c_str(), "%d:%d", &hours, &minutes);
  
  time_t now = time(NULL);
  struct tm utcTimeInfo;
  gmtime_r(&now, &utcTimeInfo);
  
  struct tm localTimeInfo;
  localtime_r(&now, &localTimeInfo);
  
  // Calculate the timezone offset in seconds
  time_t gmtTime = mktime(&utcTimeInfo);
  time_t localTime = mktime(&localTimeInfo);
  int tzOffset = difftime(localTime, gmtTime);
  
  // Create a UTC time point for the specified hours and minutes
  struct tm targetUtc = utcTimeInfo;
  targetUtc.tm_hour = hours;
  targetUtc.tm_min = minutes;
  targetUtc.tm_sec = 0;
  
  // Convert UTC time to time_t, then apply the timezone offset
  time_t targetUtcTime = mktime(&targetUtc);
  time_t targetLocalTime = targetUtcTime + tzOffset;
  
  // Convert back to a time structure
  struct tm targetLocalTm;
  localtime_r(&targetLocalTime, &targetLocalTm);
  
  // Format the time
  char buffer[6]; // HH:MM\0
  sprintf(buffer, "%02d:%02d", targetLocalTm.tm_hour, targetLocalTm.tm_min);
  return String(buffer);
}

// Initialize the scheduler system
void initScheduler() {
  debugPrintln("Initializing Scheduler system");
  
  // Initialize NTP time first
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  // Try to load existing schedules
  loadSchedulerState();
  
  // If no schedules exist, create a default empty schedule
  if (schedulerState.scheduleCount == 0) {
    debugPrintln("No schedules found, creating default empty schedule");
    addNewSchedule("Default Schedule");
  }
  
  // Don't start the scheduler task automatically
  // It will be started when the user activates the scheduler
  debugPrintln("Scheduler initialized successfully");
}

// Start the scheduler task
void startSchedulerTask() {
  if (schedulerTaskHandle == NULL) {
    schedulerActive = true;
    xTaskCreatePinnedToCore(
      [](void* parameter) {
        debugPrintln("Scheduler task started");
        
        while (true) {
          if (schedulerActive) {
            // Check and execute scheduled events
            checkAndExecuteScheduledEvents();
          }
          // Check every second
          vTaskDelay(pdMS_TO_TICKS(1000));
        }
      },
      "SchedulerTask",
      4096,
      NULL,
      1,
      &schedulerTaskHandle,
      1
    );
    debugPrintln("Scheduler task created");
  } else {
    schedulerActive = true;
    debugPrintln("Scheduler activated");
  }
}

// Stop the scheduler task
void stopSchedulerTask() {
  schedulerActive = false;
  debugPrintln("Scheduler deactivated");
}

// Check and execute any scheduled events
void checkAndExecuteScheduledEvents() {
  // Get current time in UTC
  time_t now = time(NULL);
  struct tm utcTime;
  gmtime_r(&now, &utcTime);
  
  // Only check events if we have active schedules and we're at a new minute
  if (now == lastEventCheckTime || schedulerState.scheduleCount == 0) {
    return;
  }
  
  lastEventCheckTime = now;
  
  // Current time in minutes since midnight (UTC)
  int currentMinute = utcTime.tm_hour * 60 + utcTime.tm_min;
  
  // Iterate through all schedules
  for (int scheduleIdx = 0; scheduleIdx < schedulerState.scheduleCount; scheduleIdx++) {
    Schedule& schedule = schedulerState.schedules[scheduleIdx];
    
    // Skip inactive schedules (those without assigned relays)
    if (schedule.relayMask == 0) {
      continue;
    }
    
    // Check each event in this schedule
    for (int eventIdx = 0; eventIdx < schedule.eventCount; eventIdx++) {
      Event& event = schedule.events[eventIdx];
      
      // Parse event time
      int eventHour = 0, eventMinute = 0;
      sscanf(event.time.c_str(), "%d:%d", &eventHour, &eventMinute);
      int eventStartMinute = eventHour * 60 + eventMinute;
      
      // Check if this event should start now
      if (eventStartMinute == currentMinute && (event.executedMask & 0x01) == 0) {
        debugPrintf("Executing event from schedule '%s': time %s, duration %d seconds\n", 
                   schedule.name.c_str(), event.time.c_str(), event.duration);
        
        // Set the executed flag for this event
        event.executedMask |= 0x01;
        
        // Activate the relays specified by the schedule's relay mask
        for (int relay = 0; relay < 8; relay++) {
          if (schedule.relayMask & (1 << relay)) {
            executeRelayCommand(relay, event.duration);
          }
        }
      }
    }
  }
}

// Create an event controlled by a timer to activate and deactivate a relay
void executeRelayCommand(uint8_t relay, uint16_t duration) {
  // Structure to pass parameters to the relay task
  struct RelayTaskParams {
    uint8_t relay;
    uint16_t duration;
  };
  
  // Allocate memory for parameters
  RelayTaskParams* params = new RelayTaskParams;
  params->relay = relay;
  params->duration = duration;
  
  // Create a task that will turn on the relay and then turn it off after the duration
  xTaskCreatePinnedToCore(
    [](void* parameter) {
      RelayTaskParams* params = (RelayTaskParams*)parameter;
      
      // Turn on the relay
      setRelay(params->relay, true);
      debugPrintf("Relay %d turned ON, will remain on for %d seconds\n", 
                 params->relay, params->duration);
      
      // Wait for the specified duration
      vTaskDelay(pdMS_TO_TICKS(params->duration * 1000));
      
      // Turn off the relay
      setRelay(params->relay, false);
      debugPrintf("Relay %d turned OFF after %d seconds\n", 
                 params->relay, params->duration);
      
      // Clean up
      delete params;
      vTaskDelete(NULL);
    },
    "RelayTask",
    2048,
    params,
    1,
    NULL,
    1
  );
}

// Create a new empty schedule
void addNewSchedule(const String& name) {
  if (schedulerState.scheduleCount >= MAX_SCHEDULES) {
    debugPrintln("Cannot add new schedule: maximum number of schedules reached");
    return;
  }
  
  // Get the current time
  time_t now = time(NULL);
  struct tm timeInfo;
  localtime_r(&now, &timeInfo);
  
  // Format the current date/time as metadata
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "Created on %Y-%m-%d %H:%M", &timeInfo);
  
  // Create the new schedule
  Schedule& newSchedule = schedulerState.schedules[schedulerState.scheduleCount];
  newSchedule.name = name;
  newSchedule.metadata = timeStr;
  newSchedule.relayMask = 0; // No relays assigned (inactive)
  newSchedule.lightsOnTime = "06:00"; // Default in UTC
  newSchedule.lightsOffTime = "18:00"; // Default in UTC
  newSchedule.eventCount = 0;
  
  // Increment the schedule count
  schedulerState.scheduleCount++;
  
  // Save to SPIFFS
  saveSchedulerState();
  
  debugPrintf("Created new schedule '%s', total schedules: %d\n", 
             name.c_str(), schedulerState.scheduleCount);
}

// Load scheduler state from SPIFFS
void loadSchedulerState() {
  debugPrintln("Loading scheduler state from SPIFFS");
  
  // Initialize empty state
  schedulerState.scheduleCount = 0;
  schedulerState.currentScheduleIndex = 0;
  
  // Check if the scheduler file exists
  if (!SPIFFS.exists(SCHEDULER_FILE)) {
    debugPrintln("Scheduler file not found, using defaults");
    return;
  }
  
  // Open the file
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_READ);
  if (!file) {
    debugPrintln("Failed to open scheduler file for reading");
    return;
  }
  
  // Check file size
  size_t size = file.size();
  debugPrintf("Scheduler file size: %d bytes\n", size);
  if (size == 0) {
    debugPrintln("Scheduler file is empty");
    file.close();
    return;
  }
  
  // Parse JSON
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  // Check for parsing errors
  if (error) {
    debugPrintf("Failed to parse scheduler JSON: %s\n", error.c_str());
    return;
  }
  
  // Load scheduler state
  schedulerState.scheduleCount = doc["scheduleCount"] | 0;
  schedulerState.currentScheduleIndex = doc["currentScheduleIndex"] | 0;
  
  // Load schedules
  JsonArray schedules = doc["schedules"].as<JsonArray>();
  schedulerState.scheduleCount = 0; // Reset counter
  for (JsonObject schObj : schedules) {
    if (schedulerState.scheduleCount < MAX_SCHEDULES) {
      Schedule& sch = schedulerState.schedules[schedulerState.scheduleCount];
      sch.name = schObj["name"].as<String>();
      sch.metadata = schObj["metadata"].as<String>();
      sch.relayMask = schObj["relayMask"].as<uint8_t>();
      sch.lightsOnTime = schObj["lightsOnTime"].as<String>();
      sch.lightsOffTime = schObj["lightsOffTime"].as<String>();
      
      // Load events
      JsonArray events = schObj["events"].as<JsonArray>();
      sch.eventCount = 0;
      for (JsonObject evt : events) {
        if (sch.eventCount < MAX_EVENTS) {
          Event& e = sch.events[sch.eventCount];
          e.id = evt["id"].as<String>();
          e.time = evt["time"].as<String>();
          e.duration = evt["duration"].as<uint16_t>();
          e.executedMask = 0; // Reset execution flag
          sch.eventCount++;
        }
      }
      schedulerState.scheduleCount++;
    }
  }
  
  debugPrintf("Loaded %d schedules from SPIFFS\n", schedulerState.scheduleCount);
}

// Save scheduler state to SPIFFS
void saveSchedulerState() {
  debugPrintln("Saving scheduler state to SPIFFS");
  
  // Create JSON document
  DynamicJsonDocument doc(4096);
  doc["scheduleCount"] = schedulerState.scheduleCount;
  doc["currentScheduleIndex"] = schedulerState.currentScheduleIndex;
  
  // Add schedules
  JsonArray schedules = doc.createNestedArray("schedules");
  for (int i = 0; i < schedulerState.scheduleCount; i++) {
    Schedule& sch = schedulerState.schedules[i];
    JsonObject schObj = schedules.createNestedObject();
    schObj["name"] = sch.name;
    schObj["metadata"] = sch.metadata;
    schObj["relayMask"] = sch.relayMask;
    schObj["lightsOnTime"] = sch.lightsOnTime;
    schObj["lightsOffTime"] = sch.lightsOffTime;
    
    // Add events
    JsonArray events = schObj.createNestedArray("events");
    for (int j = 0; j < sch.eventCount; j++) {
      Event& evt = sch.events[j];
      JsonObject evtObj = events.createNestedObject();
      evtObj["id"] = evt.id;
      evtObj["time"] = evt.time;
      evtObj["duration"] = evt.duration;
    }
  }
  
  // Open file for writing
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_WRITE);
  if (!file) {
    debugPrintln("Failed to open scheduler file for writing");
    return;
  }
  
  // Write JSON to file
  if (serializeJson(doc, file) == 0) {
    debugPrintln("Failed to write to scheduler file");
  }
  
  file.close();
  debugPrintln("Scheduler state saved to SPIFFS");
}

// API Handlers
void handleLoadSchedulerState(AsyncWebServerRequest *request) {
  debugPrintln("API request: Load scheduler state");
  
  // Create JSON document
  DynamicJsonDocument doc(4096);
  doc["scheduleCount"] = schedulerState.scheduleCount;
  doc["currentScheduleIndex"] = schedulerState.currentScheduleIndex;
  
  // Build relay ownership map
  uint8_t relayOwnership[8] = {0}; // Which schedule owns which relay (0 = unassigned)
  for (int i = 0; i < schedulerState.scheduleCount; i++) {
    Schedule& sch = schedulerState.schedules[i];
    uint8_t mask = sch.relayMask;
    
    for (int relay = 0; relay < 8; relay++) {
      if (mask & (1 << relay)) {
        relayOwnership[relay] = i + 1; // Store 1-based index
      }
    }
  }
  
  // Add relay ownership information
  JsonArray relayInfo = doc.createNestedArray("relayAssignments");
  for (int relay = 0; relay < 8; relay++) {
    JsonObject relayObj = relayInfo.createNestedObject();
    relayObj["relay"] = relay;
    relayObj["assignedToSchedule"] = relayOwnership[relay] - 1; // Convert back to 0-based (-1 means unassigned)
    relayObj["assignedToScheduleName"] = relayOwnership[relay] > 0 ? 
      schedulerState.schedules[relayOwnership[relay] - 1].name : "";
  }
  
  // Add schedules
  JsonArray schedules = doc.createNestedArray("schedules");
  for (int i = 0; i < schedulerState.scheduleCount; i++) {
    Schedule& sch = schedulerState.schedules[i];
    JsonObject schObj = schedules.createNestedObject();
    schObj["name"] = sch.name;
    schObj["metadata"] = sch.metadata;
    schObj["relayMask"] = sch.relayMask;
    
    // Convert UTC times to local time for the frontend
    schObj["lightsOnTime"] = utcToLocalTime(sch.lightsOnTime);
    schObj["lightsOffTime"] = utcToLocalTime(sch.lightsOffTime);
    
    // Add events
    JsonArray events = schObj.createNestedArray("events");
    for (int j = 0; j < sch.eventCount; j++) {
      Event& evt = sch.events[j];
      JsonObject evtObj = events.createNestedObject();
      evtObj["id"] = evt.id;
      // Convert UTC times to local time for the frontend
      evtObj["time"] = utcToLocalTime(evt.time);
      evtObj["duration"] = evt.duration;
    }
  }
  
  // Serialize and send response
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

// Add this helper function before handleSaveSchedulerState

// Check if relays are already assigned to other schedules
bool validateRelayAssignments(JsonArray schedules, int currentScheduleIndex) {
  uint8_t relayAssignmentMap[8] = {0}; // Tracks which schedule owns which relay
  
  int scheduleIndex = 0;
  for (JsonObject schObj : schedules) {
    uint8_t relayMask = schObj["relayMask"].as<uint8_t>();
    
    // Skip the current schedule being edited
    if (scheduleIndex == currentScheduleIndex) {
      scheduleIndex++;
      continue;
    }
    
    // Check each relay in this schedule's mask
    for (int relay = 0; relay < 8; relay++) {
      if (relayMask & (1 << relay)) {
        if (relayAssignmentMap[relay] != 0) {
          // This relay is already assigned to another schedule
          debugPrintf("Relay %d is already assigned to schedule %d\n", 
                     relay, relayAssignmentMap[relay]);
          return false;
        }
        relayAssignmentMap[relay] = scheduleIndex + 1; // Store 1-based index
      }
    }
    scheduleIndex++;
  }
  
  return true;
}

void handleSaveSchedulerState(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  static uint8_t *jsonBuffer = NULL;
  static size_t bufferSize = 0;
  
  debugPrintln("API request: Save scheduler state");
  
  // First chunk or single chunk
  if (index == 0) {
    // Free previous buffer if it exists
    if (jsonBuffer) {
      free(jsonBuffer);
      jsonBuffer = NULL;
      bufferSize = 0;
    }
    
    // Allocate new buffer with extra space as safety margin
    size_t allocSize = total + 64; // Add extra padding
    jsonBuffer = (uint8_t*)malloc(allocSize);
    if (!jsonBuffer) {
      debugPrintln("Failed to allocate memory for JSON data");
      debugPrintf("Attempted to allocate: %d bytes\n", allocSize);
      debugPrintf("Free heap: %d bytes\n", ESP.getFreeHeap());
      request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Memory allocation failed\"}");
      return;
    }
    bufferSize = allocSize;
    debugPrintf("Buffer allocated: %d bytes (requested: %d)\n", bufferSize, total);
  }
  
  // Make sure buffer exists and has enough space
  if (!jsonBuffer || bufferSize < index + len) {
    debugPrintln("JSON buffer error or buffer too small");
    debugPrintf("Buffer size: %d, index: %d, chunk length: %d\n", bufferSize, index, len);
    request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Buffer error\"}");
    if (jsonBuffer) {
      free(jsonBuffer);
      jsonBuffer = NULL;
    }
    bufferSize = 0;
    return;
  }
  
  // Copy this chunk to the buffer
  memcpy(jsonBuffer + index, data, len);
  
  debugPrintf("Received data chunk: %d bytes, index: %d, total: %d\n", len, index, total);
  
  // Only parse after receiving all chunks
  if (index + len == total) {
    debugPrintf("All data received (%d bytes), parsing JSON\n", total);
    
    // Add null terminator to treat as string
    jsonBuffer[total] = 0;
    
    // Parse JSON with larger buffer
    DynamicJsonDocument doc(16384); // Further increased buffer size
    DeserializationError error = deserializeJson(doc, jsonBuffer, total);
    
    // Free the buffer after use
    free(jsonBuffer);
    jsonBuffer = NULL;
    bufferSize = 0;
    
    // Check for parsing errors
    if (error) {
      debugPrintf("Failed to parse scheduler JSON: %s\n", error.c_str());
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
      return;
    }
    
    // Check for relay assignment conflicts
    JsonArray schedules = doc["schedules"].as<JsonArray>();
    int currentScheduleIndex = doc["currentScheduleIndex"] | 0;

    if (!validateRelayAssignments(schedules, currentScheduleIndex)) {
      debugPrintln("ERROR: Relay assignment conflict detected");
      request->send(400, "application/json", 
        "{\"status\":\"error\",\"message\":\"One or more relays are already assigned to another schedule\"}");
      return;
    }

    // Update scheduler state from JSON
    schedulerState.scheduleCount = doc["scheduleCount"] | 0;
    schedulerState.currentScheduleIndex = doc["currentScheduleIndex"] | 0;

    // Clear all schedules first
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      schedulerState.schedules[i].eventCount = 0;
    }

    // Process schedules
    schedulerState.scheduleCount = 0;
    for (JsonObject schObj : schedules) {
      if (schedulerState.scheduleCount >= MAX_SCHEDULES) break;
      
      Schedule& sch = schedulerState.schedules[schedulerState.scheduleCount];
      sch.name = schObj["name"].as<String>();
      sch.metadata = schObj["metadata"].as<String>();
      sch.relayMask = schObj["relayMask"].as<uint8_t>();
      
      // Convert local times back to UTC for storage
      sch.lightsOnTime = localTimeToUTC(schObj["lightsOnTime"].as<String>());
      sch.lightsOffTime = localTimeToUTC(schObj["lightsOffTime"].as<String>());
      
      // Process events
      sch.eventCount = 0;
      JsonArray events = schObj["events"].as<JsonArray>();
      for (JsonObject evt : events) {
        if (sch.eventCount >= MAX_EVENTS) break;
        
        Event& e = sch.events[sch.eventCount];
        e.id = evt["id"].as<String>();
        // Convert local time back to UTC for storage
        e.time = localTimeToUTC(evt["time"].as<String>());
        e.duration = evt["duration"].as<uint16_t>();
        e.executedMask = 0; // Reset execution flag
        
        sch.eventCount++;
      }
      
      schedulerState.scheduleCount++;
    }

    debugPrintf("Updated scheduler state with %d schedules\n", schedulerState.scheduleCount);
    
    // After processing, save to SPIFFS
    saveSchedulerState();
    
    // Send success response
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler state saved\"}");
  }
}

void handleSchedulerStatus(AsyncWebServerRequest *request) {
  debugPrintln("API request: Scheduler status");
  
  // Create JSON document
  DynamicJsonDocument doc(512);
  doc["isActive"] = schedulerActive;
  doc["scheduleCount"] = schedulerState.scheduleCount;
  
  // Serialize and send response
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void handleActivateScheduler(AsyncWebServerRequest *request) {
  debugPrintln("API request: Activate scheduler");
  startSchedulerTask();
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler activated\"}");
}

void handleDeactivateScheduler(AsyncWebServerRequest *request) {
  debugPrintln("API request: Deactivate scheduler");
  stopSchedulerTask();
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler deactivated\"}");
}

void handleManualWatering(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("API request: Manual watering");
  
  // Parse JSON
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  // Check for parsing errors
  if (error) {
    debugPrintf("Failed to parse manual watering JSON: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  // Check required fields
  if (!doc.containsKey("relay") || !doc.containsKey("duration")) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing relay or duration\"}");
    return;
  }
  
  // Get parameters
  int relay = doc["relay"].as<int>();
  int duration = doc["duration"].as<int>();
  
  // Validate parameters
  if (relay < 0 || relay > 7 || duration <= 0) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid relay or duration\"}");
    return;
  }
  
  // Execute command
  executeRelayCommand(relay, duration);
  
  // Send response
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Manual watering executed\"}");
}

// Initialize the WebSocket server
void initSchedulerWebSocket() {
  debugPrintln("Initializing scheduler WebSocket server");
  
  // Set default session state
  currentSession.sessionId = "";
  currentSession.lastActivity = 0;
  currentSession.mode = MODE_VIEW_ONLY;
  currentSession.editingScheduleIndex = -1;
  currentSession.isDirty = false;
  
  // Set up event handler
  schedulerWs.onEvent(handleWebSocketEvent);
  
  // Add to your existing web server
  server.addHandler(&schedulerWs);
  
  debugPrintln("Scheduler WebSocket server initialized");
}

// Handle WebSocket events
void handleWebSocketEvent(AsyncWebSocket* webSocket, AsyncWebSocketClient* client, 
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      debugPrintf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
      
    case WS_EVT_DISCONNECT:
      debugPrintf("WebSocket client #%u disconnected\n", client->id());
      // Don't reset session immediately, allow for reconnection
      break;
      
    case WS_EVT_DATA:
      handleWebSocketMessage(webSocket, client, (AwsFrameInfo*)arg, data, len);
      break;
      
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

// Process WebSocket messages
void handleWebSocketMessage(AsyncWebSocket* webSocket, AsyncWebSocketClient* client, 
                          AwsFrameInfo* info, uint8_t* data, size_t len) {
  // Ensure the data is a complete message
  if (info->final && info->index == 0 && info->len == len) {
    // Null terminate the received data
    data[len] = 0;
    
    // Parse JSON message
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, (char*)data);
    
    if (error) {
      debugPrintf("WebSocket JSON parse error: %s\n", error.c_str());
      
      // Send error response
      DynamicJsonDocument errorDoc(512);
      errorDoc["type"] = "error";
      errorDoc["message"] = "Invalid JSON format";
      
      String response;
      serializeJson(errorDoc, response);
      client->text(response);
      return;
    }
    
    // Update activity timestamp for timeout management
    currentSession.lastActivity = millis();
    
    // Get message type
    String messageType = doc["type"];
    debugPrintf("Received WebSocket message type: %s\n", messageType.c_str());
    
    if (messageType == "reconnect") {
      // Handle reconnection with session ID
      String sessionId = doc["sessionId"];
      if (sessionId == currentSession.sessionId) {
        // Resume existing session
        DynamicJsonDocument responseDoc(4096);
        responseDoc["type"] = "session_restored";
        responseDoc["mode"] = currentSession.mode;
        
        if (currentSession.mode == MODE_EDITING) {
          responseDoc["editingIndex"] = currentSession.editingScheduleIndex;
        }
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
      } else {
        // Invalid session, revert to view-only mode
        resetSession();
        
        DynamicJsonDocument responseDoc(512);
        responseDoc["type"] = "session_expired";
        responseDoc["mode"] = MODE_VIEW_ONLY;
        
        String response;
        serializeJson(responseDoc, response);
        client->text(response);
      }
    }
    else if (messageType == "start_create") {
      // Start creating a new schedule
      if (currentSession.mode != MODE_VIEW_ONLY) {
        String modeStr = (currentSession.mode == MODE_CREATING ? "create" : "edit");
        sendErrorResponse(client, "Cannot create new schedule while in " + modeStr + " mode");
        return;
      }
      
      // Generate new session ID
      currentSession.sessionId = generateSessionId();
      currentSession.mode = MODE_CREATING;
      currentSession.editingScheduleIndex = -1;
      currentSession.isDirty = false;
      
      // Initialize empty pending schedule
      currentSession.pendingSchedule = {};
      currentSession.pendingSchedule.name = "New Schedule";
      currentSession.pendingSchedule.relayMask = 0;
      currentSession.pendingSchedule.lightsOnTime = "06:00";  // Default UTC time
      currentSession.pendingSchedule.lightsOffTime = "18:00"; // Default UTC time
      currentSession.pendingSchedule.eventCount = 0;
      
      // Get the current time for metadata
      time_t now = time(NULL);
      struct tm timeInfo;
      localtime_r(&now, &timeInfo);
      char timeStr[64];
      strftime(timeStr, sizeof(timeStr), "Created on %Y-%m-%d %H:%M", &timeInfo);
      currentSession.pendingSchedule.metadata = timeStr;
      
      // Send response
      DynamicJsonDocument responseDoc(4096);
      responseDoc["type"] = "create_started";
      responseDoc["sessionId"] = currentSession.sessionId;
      responseDoc["schedule"] = serializeSchedule(currentSession.pendingSchedule, true); // Convert times to local
      
      String response;
      serializeJson(responseDoc, response);
      client->text(response);
    }
    else if (messageType == "start_edit") {
      // Start editing an existing schedule
      if (currentSession.mode != MODE_VIEW_ONLY) {
        String modeStr = (currentSession.mode == MODE_CREATING ? "create" : "edit");
        sendErrorResponse(client, "Cannot edit schedule while in " + modeStr + " mode");
        return;
      }
      
      int scheduleIndex = doc["scheduleIndex"];
      if (scheduleIndex < 0 || scheduleIndex >= schedulerState.scheduleCount) {
        sendErrorResponse(client, "Invalid schedule index");
        return;
      }
      
      // Generate new session ID
      currentSession.sessionId = generateSessionId();
      currentSession.mode = MODE_EDITING;
      currentSession.editingScheduleIndex = scheduleIndex;
      currentSession.isDirty = false;
      
      // Create a copy of the schedule for editing
      currentSession.pendingSchedule = schedulerState.schedules[scheduleIndex];
      
      // Send response
      DynamicJsonDocument responseDoc(4096);
      responseDoc["type"] = "edit_started";
      responseDoc["sessionId"] = currentSession.sessionId;
      responseDoc["scheduleIndex"] = scheduleIndex;
      responseDoc["schedule"] = serializeSchedule(currentSession.pendingSchedule, true); // Convert times to local
      
      String response;
      serializeJson(responseDoc, response);
      client->text(response);
    }
    else if (messageType == "update_schedule") {
      // Update the pending schedule
      if (currentSession.mode == MODE_VIEW_ONLY) {
        sendErrorResponse(client, "Cannot update schedule in view-only mode");
        return;
      }
      
      // Verify session ID
      String sessionId = doc["sessionId"];
      if (sessionId != currentSession.sessionId) {
        sendErrorResponse(client, "Invalid session ID");
        return;
      }
      
      // Get schedule updates
      JsonObject scheduleData = doc["schedule"];
      
      // Update the pending schedule
      if (scheduleData.containsKey("name")) {
        currentSession.pendingSchedule.name = scheduleData["name"].as<String>();
      }
      
      if (scheduleData.containsKey("relayMask")) {
        currentSession.pendingSchedule.relayMask = scheduleData["relayMask"].as<uint8_t>();
      }
      
      if (scheduleData.containsKey("lightsOnTime")) {
        // Convert from local to UTC
        currentSession.pendingSchedule.lightsOnTime = 
          localTimeToUTC(scheduleData["lightsOnTime"].as<String>());
      }
      
      if (scheduleData.containsKey("lightsOffTime")) {
        // Convert from local to UTC
        currentSession.pendingSchedule.lightsOffTime = 
          localTimeToUTC(scheduleData["lightsOffTime"].as<String>());
      }
      
      // Handle events if present
      if (scheduleData.containsKey("events")) {
        JsonArray events = scheduleData["events"].as<JsonArray>();
        currentSession.pendingSchedule.eventCount = 0;
        
        for (JsonObject evt : events) {
          if (currentSession.pendingSchedule.eventCount >= MAX_EVENTS) break;
          
          Event& e = currentSession.pendingSchedule.events[currentSession.pendingSchedule.eventCount];
          e.id = evt["id"].as<String>();
          e.time = localTimeToUTC(evt["time"].as<String>());
          e.duration = evt["duration"].as<uint16_t>();
          e.executedMask = 0; // Reset execution flag
          
          currentSession.pendingSchedule.eventCount++;
        }
      }
      
      currentSession.isDirty = true;
      
      // Send acknowledgement
      DynamicJsonDocument responseDoc(512);
      responseDoc["type"] = "update_acknowledged";
      responseDoc["sessionId"] = currentSession.sessionId;
      
      String response;
      serializeJson(responseDoc, response);
      client->text(response);
    }
    else if (messageType == "save_schedule") {
      // Save the pending schedule
      if (currentSession.mode == MODE_VIEW_ONLY) {
        sendErrorResponse(client, "Cannot save schedule in view-only mode");
        return;
      }
      
      // Verify session ID
      String sessionId = doc["sessionId"];
      if (sessionId != currentSession.sessionId) {
        sendErrorResponse(client, "Invalid session ID");
        return;
      }
      
      // For creating mode, append new schedule
      if (currentSession.mode == MODE_CREATING) {
        if (schedulerState.scheduleCount >= MAX_SCHEDULES) {
          sendErrorResponse(client, "Maximum number of schedules reached");
          return;
        }
        
        // Add the new schedule
        schedulerState.schedules[schedulerState.scheduleCount] = currentSession.pendingSchedule;
        schedulerState.scheduleCount++;
      }
      // For editing mode, update existing schedule
      else if (currentSession.mode == MODE_EDITING) {
        schedulerState.schedules[currentSession.editingScheduleIndex] = currentSession.pendingSchedule;
      }
      
      // Save to SPIFFS
      saveSchedulerState();
      
      // Reset session to view-only mode
      resetSession();
      
      // Send success response
      DynamicJsonDocument responseDoc(512);
      responseDoc["type"] = "save_successful";
      responseDoc["mode"] = MODE_VIEW_ONLY;
      
      String response;
      serializeJson(responseDoc, response);
      client->text(response);
      
      // Broadcast to all clients that data has changed
      broadcastSchedulerUpdate();
    }
    else if (messageType == "cancel") {
      // Cancel editing/creating
      if (currentSession.mode == MODE_VIEW_ONLY) {
        sendErrorResponse(client, "Already in view-only mode");
        return;
      }
      
      // Verify session ID
      String sessionId = doc["sessionId"];
      if (sessionId != currentSession.sessionId) {
        sendErrorResponse(client, "Invalid session ID");
        return;
      }
      
      // Reset session
      resetSession();
      
      // Send acknowledgement
      DynamicJsonDocument responseDoc(512);
      responseDoc["type"] = "edit_cancelled";
      responseDoc["mode"] = MODE_VIEW_ONLY;
      
      String response;
      serializeJson(responseDoc, response);
      client->text(response);
    }
    else if (messageType == "delete_schedule") {
      // Delete a schedule
      int scheduleIndex = doc["scheduleIndex"];
      if (scheduleIndex < 0 || scheduleIndex >= schedulerState.scheduleCount) {
        sendErrorResponse(client, "Invalid schedule index");
        return;
      }
      
      // Shift remaining schedules
      for (int i = scheduleIndex; i < schedulerState.scheduleCount - 1; i++) {
        schedulerState.schedules[i] = schedulerState.schedules[i + 1];
      }
      
      // Decrement count
      schedulerState.scheduleCount--;
      
      // Update current index if needed
      if (schedulerState.currentScheduleIndex >= schedulerState.scheduleCount) {
        schedulerState.currentScheduleIndex = schedulerState.scheduleCount > 0 ? 
                                             schedulerState.scheduleCount - 1 : 0;
      }
      
      // Save changes
      saveSchedulerState();
      
      // Reset any active editing session
      resetSession();
      
      // Send success response
      DynamicJsonDocument responseDoc(512);
      responseDoc["type"] = "delete_successful";
      
      String response;
      serializeJson(responseDoc, response);
      client->text(response);
      
      // Broadcast to all clients that data has changed
      broadcastSchedulerUpdate();
    }
    else if (messageType == "get_state") {
      // Send current scheduler state
      sendSchedulerState(client);
    }
    else {
      // Unknown message type
      sendErrorResponse(client, "Unknown message type: " + messageType);
    }
  }
}

// Helper to serialize a schedule to JSON
JsonObject serializeSchedule(Schedule& schedule, bool convertToLocalTime) {
  DynamicJsonDocument doc(4096);
  JsonObject obj = doc.to<JsonObject>();
  
  obj["name"] = schedule.name;
  obj["metadata"] = schedule.metadata;
  obj["relayMask"] = schedule.relayMask;
  
  // Handle time conversion
  if (convertToLocalTime) {
    obj["lightsOnTime"] = utcToLocalTime(schedule.lightsOnTime);
    obj["lightsOffTime"] = utcToLocalTime(schedule.lightsOffTime);
  } else {
    obj["lightsOnTime"] = schedule.lightsOnTime;
    obj["lightsOffTime"] = schedule.lightsOffTime;
  }
  
  // Add events
  JsonArray events = obj.createNestedArray("events");
  for (int i = 0; i < schedule.eventCount; i++) {
    Event& evt = schedule.events[i];
    JsonObject evtObj = events.createNestedObject();
    evtObj["id"] = evt.id;
    
    if (convertToLocalTime) {
      evtObj["time"] = utcToLocalTime(evt.time);
    } else {
      evtObj["time"] = evt.time;
    }
    
    evtObj["duration"] = evt.duration;
  }
  
  return obj;
}

// Send current scheduler state to client
void sendSchedulerState(AsyncWebSocketClient* client) {
  DynamicJsonDocument doc(8192);
  doc["type"] = "scheduler_state";
  doc["scheduleCount"] = schedulerState.scheduleCount;
  doc["currentScheduleIndex"] = schedulerState.currentScheduleIndex;
  doc["mode"] = currentSession.mode;
  
  if (currentSession.mode != MODE_VIEW_ONLY) {
    doc["sessionId"] = currentSession.sessionId;
    doc["editingIndex"] = currentSession.editingScheduleIndex;
  }
  
  // Add schedules
  JsonArray schedules = doc.createNestedArray("schedules");
  for (int i = 0; i < schedulerState.scheduleCount; i++) {
    JsonObject schObj = schedules.createNestedObject();
    Schedule& sch = schedulerState.schedules[i];
    
    schObj["name"] = sch.name;
    schObj["metadata"] = sch.metadata;
    schObj["relayMask"] = sch.relayMask;
    schObj["lightsOnTime"] = utcToLocalTime(sch.lightsOnTime);
    schObj["lightsOffTime"] = utcToLocalTime(sch.lightsOffTime);
    
    // Add events
    JsonArray events = schObj.createNestedArray("events");
    for (int j = 0; j < sch.eventCount; j++) {
      JsonObject evtObj = events.createNestedObject();
      Event& evt = sch.events[j];
      
      evtObj["id"] = evt.id;
      evtObj["time"] = utcToLocalTime(evt.time);
      evtObj["duration"] = evt.duration;
    }
  }
  
  String response;
  serializeJson(doc, response);
  client->text(response);
}

// Reset the editing session
void resetSession() {
  currentSession.sessionId = "";
  currentSession.lastActivity = 0;
  currentSession.mode = MODE_VIEW_ONLY;
  currentSession.editingScheduleIndex = -1;
  currentSession.isDirty = false;
}

// Generate a random session ID
String generateSessionId() {
  const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  String result = "";
  
  for (int i = 0; i < 16; i++) {
    int index = random(0, strlen(charset));
    result += charset[index];
  }
  
  return result;
}

// Send error response to client
void sendErrorResponse(AsyncWebSocketClient* client, const String& message) {
  DynamicJsonDocument doc(512);
  doc["type"] = "error";
  doc["message"] = message;
  
  String response;
  serializeJson(doc, response);
  client->text(response);
}

// Broadcast scheduler update to all connected clients
void broadcastSchedulerUpdate() {
  DynamicJsonDocument doc(512);
  doc["type"] = "data_changed";
  
  String response;
  serializeJson(doc, response);
  schedulerWs.textAll(response);
}

// Check for session timeout
void checkSchedulerTimeouts() {
  unsigned long currentTime = millis();
  
  // Only check periodically to save CPU
  if (currentTime - lastTimeoutCheck < 10000) return; // Check every 10 seconds
  
  lastTimeoutCheck = currentTime;
  
  // If there's an active session, check if it has timed out
  if (currentSession.mode != MODE_VIEW_ONLY && currentSession.lastActivity > 0) {
    // Handle rollover case
    unsigned long elapsed = (currentTime >= currentSession.lastActivity) ? 
                          (currentTime - currentSession.lastActivity) : 
                          (UINT32_MAX - currentSession.lastActivity + currentTime);
    
    if (elapsed > SCHEDULER_TIMEOUT_MS) {
      debugPrintln("Scheduler editing session timed out");
      
      // Reset session
      resetSession();
      
      // Notify all clients
      DynamicJsonDocument doc(512);
      doc["type"] = "session_timeout";
      doc["mode"] = MODE_VIEW_ONLY;
      
      String response;
      serializeJson(doc, response);
      schedulerWs.textAll(response);
    }
  }
}

// Call this from your main loop or a periodic task
void updateSchedulerWebSocket() {
  schedulerWs.cleanupClients();
  checkSchedulerTimeouts();
}