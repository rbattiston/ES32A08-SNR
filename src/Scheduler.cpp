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

// If timegm is not available on your platform, use this implementation:
time_t timegm(struct tm *tm) {
  time_t ret;
  char *tz;
  
  tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzset();
  ret = mktime(tm);
  if (tz)
    setenv("TZ", tz, 1);
  else
    unsetenv("TZ");
  tzset();
  return ret;
}

// Convert times between local and UTC formats
String localTimeToUTC(const String& localTime) {
  // Parse the input time
  int hours, minutes;
  if (sscanf(localTime.c_str(), "%d:%d", &hours, &minutes) != 2) {
    debugPrintf("ERROR: Invalid time format: %s\n", localTime.c_str());
    return localTime; // Return unchanged if format is invalid
  }
  
  debugPrintf("DEBUG: Converting local time %02d:%02d to UTC\n", hours, minutes);
  
  // Get current time to determine time zone offset
  time_t now = time(NULL);
  
  // Get local and UTC time structures
  struct tm localTimeInfo;
  localtime_r(&now, &localTimeInfo);
  
  // Create a tm structure with today's date but the specified local time
  struct tm targetLocalTime = localTimeInfo;
  targetLocalTime.tm_hour = hours;
  targetLocalTime.tm_min = minutes;
  targetLocalTime.tm_sec = 0;
  
  // Convert local time structure to time_t (seconds since epoch)
  time_t targetLocalTimeT = mktime(&targetLocalTime);
  
  // Convert to UTC time structure
  struct tm targetUtcTime;
  gmtime_r(&targetLocalTimeT, &targetUtcTime);
  
  // Format the UTC time as a string
  char buffer[6]; // HH:MM\0
  sprintf(buffer, "%02d:%02d", targetUtcTime.tm_hour, targetUtcTime.tm_min);
  
  debugPrintf("DEBUG: Converted to UTC time %s\n", buffer);
  return String(buffer);
}

String utcToLocalTime(const String& utcTime) {
  // Parse the input time
  int hours, minutes;
  if (sscanf(utcTime.c_str(), "%d:%d", &hours, &minutes) != 2) {
    debugPrintf("ERROR: Invalid time format: %s\n", utcTime.c_str());
    return utcTime; // Return unchanged if format is invalid
  }
  
  debugPrintf("DEBUG: Converting UTC time %02d:%02d to local time\n", hours, minutes);
  
  // Get current time
  time_t now = time(NULL);
  
  // Get UTC time structure
  struct tm utcTimeInfo;
  gmtime_r(&now, &utcTimeInfo);
  
  // Create a tm structure with today's date but the specified UTC time
  struct tm targetUtcTime = utcTimeInfo;
  targetUtcTime.tm_hour = hours;
  targetUtcTime.tm_min = minutes;
  targetUtcTime.tm_sec = 0;
  
  // Convert UTC time to time_t using a workaround for timegm
  // We temporarily change timezone to UTC, call mktime, then restore timezone
  char* tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzset();
  time_t targetUtcTimeT = mktime(&targetUtcTime);
  if (tz)
    setenv("TZ", tz, 1);
  else
    unsetenv("TZ");
  tzset();
  
  // Convert to local time structure
  struct tm targetLocalTime;
  localtime_r(&targetUtcTimeT, &targetLocalTime);
  
  // Format the local time as a string
  char buffer[6]; // HH:MM\0
  sprintf(buffer, "%02d:%02d", targetLocalTime.tm_hour, targetLocalTime.tm_min);
  
  debugPrintf("DEBUG: Converted to local time %s\n", buffer);
  return String(buffer);
}

bool isValidTimeFormat(const String& timeStr) {
  int hours, minutes;
  int parsed = sscanf(timeStr.c_str(), "%d:%d", &hours, &minutes);
  return (parsed == 2 && hours >= 0 && hours < 24 && minutes >= 0 && minutes < 60);
}


// Initialize the scheduler system
void initScheduler() {
  debugPrintln("Initializing Scheduler system");
    
  testTimeConversion();
  
  // Always start the scheduler task automatically
  startSchedulerTask();
  debugPrintln("Scheduler task started automatically");
  
  // Initialize NTP time first
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  // Try to load existing schedules
  loadSchedulerState();
  
  // Verify that time is correctly synchronized
  if (!verifyTimeSync()) {
    debugPrintln("WARNING: Time synchronization issue detected - scheduler may not function correctly");
  }
  
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

// This is an improved version of the checkAndExecuteScheduledEvents function for src/Scheduler.cpp

void checkAndExecuteScheduledEvents() {
  // Get current time in UTC
  time_t now = time(NULL);
  struct tm utcTime;
  gmtime_r(&now, &utcTime);
  
  static int lastHour = -1;
  
  // Print time debug info when the hour changes
  if (utcTime.tm_hour != lastHour) {
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &utcTime);
    debugPrintf("DEBUG: Current UTC time: %s\n", timeStr);
    
    struct tm localTime;
    localtime_r(&now, &localTime);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);
    debugPrintf("DEBUG: Current local time: %s\n", timeStr);
    
    lastHour = utcTime.tm_hour;
  }
    
  // Get current time in UTC
  now = time(NULL);
  gmtime_r(&now, &utcTime);
  
  // Only check events if we have active schedules and we're at a new minute
  if (now == lastEventCheckTime || schedulerState.scheduleCount == 0) {
    return;
  }
  
  lastEventCheckTime = now;
  
  // Format current time for logging
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &utcTime);
  debugPrintf("DEBUG: Checking scheduled events at %s UTC\n", timeStr);
  
  // Current time in minutes since midnight (UTC)
  int currentMinute = utcTime.tm_hour * 60 + utcTime.tm_min;
  
  debugPrintf("DEBUG: Checking events at %02d:%02d (minute %d)\n", 
    utcTime.tm_hour, utcTime.tm_min, currentMinute);
  
  // Iterate through all schedules
  for (int scheduleIdx = 0; scheduleIdx < schedulerState.scheduleCount; scheduleIdx++) {
    Schedule& schedule = schedulerState.schedules[scheduleIdx];
    
    // Skip inactive schedules (those without assigned relays)
    if (schedule.relayMask == 0) {
      continue;
    }
    
    debugPrintf("DEBUG: Checking schedule '%s' (relayMask: 0x%02X)\n", 
               schedule.name.c_str(), schedule.relayMask);
    
    // Check each event in this schedule
    for (int eventIdx = 0; eventIdx < schedule.eventCount; eventIdx++) {
      Event& event = schedule.events[eventIdx];
      
      // Parse event time
      int eventHour = 0, eventMinute = 0;
      sscanf(event.time.c_str(), "%d:%d", &eventHour, &eventMinute);
      int eventStartMinute = eventHour * 60 + eventMinute;
      
      // Check if this event should start now (compare minutes since midnight)
      if (eventStartMinute == currentMinute) {
        // Check if this event has already been executed today
        // Bit 0 in executedMask is for today's execution status
        bool alreadyExecuted = (event.executedMask & 0x01) != 0;
        
        if (!alreadyExecuted) {
          debugPrintf("DEBUG: Executing event from schedule '%s': time %s, duration %d seconds, relayMask 0x%02X\n", 
                     schedule.name.c_str(), event.time.c_str(), event.duration, schedule.relayMask);
          
          // Set the executed flag for this event
          event.executedMask |= 0x01;
          
          // Activate the relays specified by the schedule's relay mask
          for (int relay = 0; relay < 8; relay++) {
            if (schedule.relayMask & (1 << relay)) {
              debugPrintf("DEBUG: Activating relay %d for %d seconds\n", relay, event.duration);
              executeRelayCommand(relay, event.duration);
            }
          }
        } else {
          debugPrintf("DEBUG: Event at %s already executed today, skipping\n", event.time.c_str());
        }
      }
    }
  }
  
  // Check if day has changed since last check
  static int lastDay = -1;
  if (lastDay != utcTime.tm_mday) {
    debugPrintf("DEBUG: Day changed from %d to %d, resetting execution flags\n", 
               lastDay, utcTime.tm_mday);
    
    // Reset all execution flags when the day changes
    for (int scheduleIdx = 0; scheduleIdx < schedulerState.scheduleCount; scheduleIdx++) {
      Schedule& schedule = schedulerState.schedules[scheduleIdx];
      for (int eventIdx = 0; eventIdx < schedule.eventCount; eventIdx++) {
        schedule.events[eventIdx].executedMask = 0;
      }
    }
    
    lastDay = utcTime.tm_mday;
  }
}

// This is an improved version of the executeRelayCommand function for src/Scheduler.cpp

void executeRelayCommand(uint8_t relay, uint16_t duration) {
  // Validate parameters
  if (relay >= 8 || duration == 0) {
    debugPrintf("ERROR: Invalid relay (%d) or duration (%d)\n", relay, duration);
    return;
  }

  // Structure to pass parameters to the relay task
  struct RelayTaskParams {
    uint8_t relay;
    uint16_t duration;
  };
  
  // Allocate memory for parameters
  RelayTaskParams* params = new RelayTaskParams;
  params->relay = relay;
  params->duration = duration;
  
  debugPrintf("DEBUG: Creating relay task for relay %d, duration %d seconds\n", relay, duration);
  
  // Create a task that will turn on the relay and then turn it off after the duration
  xTaskCreatePinnedToCore(
    [](void* parameter) {
      RelayTaskParams* params = (RelayTaskParams*)parameter;
      
      // Turn on the relay
      setRelay(params->relay, true);
      debugPrintf("DEBUG: Relay %d turned ON, will remain on for %d seconds\n", 
                 params->relay, params->duration);
      
      // Wait for the specified duration
      vTaskDelay(pdMS_TO_TICKS(params->duration * 1000));
      
      // Turn off the relay
      setRelay(params->relay, false);
      debugPrintf("DEBUG: Relay %d turned OFF after %d seconds\n", 
                 params->relay, params->duration);
      
      // Clean up
      delete params;
      vTaskDelete(NULL);
    },
    "RelayTask",
    4096,  // Increased stack size for safety
    params,
    2,     // Higher priority to ensure timely execution
    NULL,
    1      // Run on core 1
  );
  
  // Log after task creation for debugging
  debugPrintln("DEBUG: Relay task created successfully");
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
// 4. Enhanced schedule loading debug output
void loadSchedulerState() {
  debugPrintln("DEBUG: Loading scheduler state from SPIFFS");
  
  // Initialize empty state
  schedulerState.scheduleCount = 0;
  schedulerState.currentScheduleIndex = 0;
  
  // Check if the scheduler file exists
  if (!SPIFFS.exists(SCHEDULER_FILE)) {
    debugPrintln("DEBUG: Scheduler file not found, using defaults");
    return;
  }
  
  // Open the file
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_READ);
  if (!file) {
    debugPrintln("DEBUG: Failed to open scheduler file for reading");
    return;
  }
  
  // Check file size
  size_t size = file.size();
  debugPrintf("DEBUG: Scheduler file size: %d bytes\n", size);
  if (size == 0) {
    debugPrintln("DEBUG: Scheduler file is empty");
    file.close();
    return;
  }
  
  // Parse JSON
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  // Check for parsing errors
  if (error) {
    debugPrintf("DEBUG: Failed to parse scheduler JSON: %s\n", error.c_str());
    return;
  }
  
  // Load scheduler state
  schedulerState.scheduleCount = doc["scheduleCount"] | 0;
  schedulerState.currentScheduleIndex = doc["currentScheduleIndex"] | 0;
  
  // Load schedules
  JsonArray schedules = doc["schedules"].as<JsonArray>();
  schedulerState.scheduleCount = 0; // Reset counter
  
  debugPrintln("DEBUG: Loading scheduler state from SPIFFS");
  
  for (JsonObject schObj : schedules) {
    if (schedulerState.scheduleCount < MAX_SCHEDULES) {
      Schedule& sch = schedulerState.schedules[schedulerState.scheduleCount];
      sch.name = schObj["name"].as<String>();
      sch.metadata = schObj["metadata"].as<String>();
      sch.relayMask = schObj["relayMask"].as<uint8_t>();
      sch.lightsOnTime = schObj["lightsOnTime"].as<String>();
      sch.lightsOffTime = schObj["lightsOffTime"].as<String>();
      
      debugPrintf("DEBUG: Loading schedule [%d]: \"%s\"\n", 
                 schedulerState.scheduleCount, sch.name.c_str());
      debugPrintf("DEBUG:   - Relay Mask: 0x%02X\n", sch.relayMask);
      debugPrintf("DEBUG:   - Lights: ON %s, OFF %s\n", 
                 sch.lightsOnTime.c_str(), sch.lightsOffTime.c_str());
      
      // Load events
      JsonArray events = schObj["events"].as<JsonArray>();
      sch.eventCount = 0;
      
      debugPrintf("DEBUG:   - Events to load: %d\n", events.size());
      
      for (JsonObject evt : events) {
        if (sch.eventCount < MAX_EVENTS) {
          Event& e = sch.events[sch.eventCount];
          e.id = evt["id"].as<String>();
          e.time = evt["time"].as<String>();
          e.duration = evt["duration"].as<uint16_t>();
          e.executedMask = 0; // Reset execution flag
          
          debugPrintf("DEBUG:     Event [%d]: Time %s, Duration %d seconds, ID \"%s\"\n", 
                     sch.eventCount, e.time.c_str(), e.duration, e.id.c_str());
          
          sch.eventCount++;
        } else {
          debugPrintf("DEBUG:     WARNING: Event limit reached (%d), skipping additional events\n", 
                     MAX_EVENTS);
          break;
        }
      }
      schedulerState.scheduleCount++;
    } else {
      debugPrintf("DEBUG:   WARNING: Schedule limit reached (%d), skipping additional schedules\n", 
                 MAX_SCHEDULES);
      break;
    }
  }
  
  debugPrintf("DEBUG: Loaded %d schedules from SPIFFS\n", schedulerState.scheduleCount);
}

// Save scheduler state to SPIFFS
// 1. Enhanced schedule saving debug output
void saveSchedulerState() {
  debugPrintln("DEBUG: Saving scheduler state to SPIFFS");
  
  // Create JSON document
  DynamicJsonDocument doc(4096);
  doc["scheduleCount"] = schedulerState.scheduleCount;
  doc["currentScheduleIndex"] = schedulerState.currentScheduleIndex;
  
  debugPrintln("DEBUG: Saving scheduler state with following schedules:");
  
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
    
    // Print schedule info
    debugPrintf("DEBUG: Schedule [%d]: \"%s\"\n", i, sch.name.c_str());
    debugPrintf("DEBUG:   - Relay Mask: 0x%02X\n", sch.relayMask);
    debugPrintf("DEBUG:   - Lights: ON %s, OFF %s\n", sch.lightsOnTime.c_str(), sch.lightsOffTime.c_str());
    debugPrintf("DEBUG:   - Event count: %d\n", sch.eventCount);
    
    // Add events
    JsonArray events = schObj.createNestedArray("events");
    for (int j = 0; j < sch.eventCount; j++) {
      Event& evt = sch.events[j];
      JsonObject evtObj = events.createNestedObject();
      evtObj["id"] = evt.id;
      evtObj["time"] = evt.time;
      evtObj["duration"] = evt.duration;
      
      // Print event info
      debugPrintf("DEBUG:     Event [%d]: Time %s, Duration %d seconds, ID \"%s\"\n", 
                 j, evt.time.c_str(), evt.duration, evt.id.c_str());
    }
  }
  
  // Open file for writing
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_WRITE);
  if (!file) {
    debugPrintln("DEBUG: Failed to open scheduler file for writing");
    return;
  }
  
  // Write JSON to file
  if (serializeJson(doc, file) == 0) {
    debugPrintln("DEBUG: Failed to write to scheduler file");
  }
  
  file.close();
  debugPrintln("DEBUG: Scheduler state saved to SPIFFS");
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
debugPrintf("WebSocket CONNECT event: Client #%u connected from %s\n", 
client->id(), client->remoteIP().toString().c_str());
debugPrintf("WebSocket details - IP: %s, Client ID: %u, Total clients: %d\n", 
client->remoteIP().toString().c_str(), 
client->id(), 
webSocket->count());
break;

case WS_EVT_DISCONNECT:
debugPrintf("WebSocket DISCONNECT event: Client #%u disconnected\n", client->id());
break;

case WS_EVT_DATA:
debugPrintf("WebSocket DATA event: Received %d bytes from client #%u\n", len, client->id());
handleWebSocketMessage(webSocket, client, (AwsFrameInfo*)arg, data, len);
break;

case WS_EVT_PONG:
debugPrintf("WebSocket PONG event received from client #%u\n", client->id());
break;

case WS_EVT_ERROR:
debugPrintf("WebSocket ERROR event for client #%u\n", client ? client->id() : 0);
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

// Scheduler monitoring task to display upcoming events and diagnostics
void schedulerMonitorTask(void *pvParameters) {
  debugPrintln("DEBUG: Scheduler monitor task started");
  
  for (;;) {
    if (schedulerState.scheduleCount > 0 && schedulerActive) {
      // Get current time
      time_t now = time(NULL);
      if (now < 0) {
        debugPrintln("DEBUG: Invalid system time, NTP sync may not be complete");
        vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds before retrying
        continue;
      }
      
      struct tm timeinfo;
      gmtime_r(&now, &timeinfo);
      
      // Format current time for output
      char currentTimeStr[32];
      strftime(currentTimeStr, sizeof(currentTimeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
      
      debugPrintln("--------- SCHEDULER MONITOR ---------");
      debugPrintf("Current time (UTC): %s\n", currentTimeStr);
      debugPrintf("Scheduler active: %s\n", schedulerActive ? "YES" : "NO");
      debugPrintf("Number of schedules: %d\n", schedulerState.scheduleCount);
      
      // Current time in minutes since midnight
      int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
      debugPrintf("Minutes since midnight: %d\n", currentMinutes);
      
      // Time until next check
      time_t secondsUntilNextMinute = 60 - timeinfo.tm_sec;
      debugPrintf("Seconds until next check: %ld\n", secondsUntilNextMinute);
      
      // Find the next upcoming event across all schedules
      time_t earliestEventTime = INT32_MAX;
      String earliestEventId = "";
      String earliestScheduleName = "";
      uint8_t earliestScheduleRelayMask = 0;
      
      for (int scheduleIdx = 0; scheduleIdx < schedulerState.scheduleCount; scheduleIdx++) {
        Schedule& schedule = schedulerState.schedules[scheduleIdx];
        
        // Skip inactive schedules (those without assigned relays)
        if (schedule.relayMask == 0) {
          continue;
        }
        
        debugPrintf("\nSchedule: %s (Relay mask: 0x%02X)\n", 
                  schedule.name.c_str(), schedule.relayMask);
        
        for (int eventIdx = 0; eventIdx < schedule.eventCount; eventIdx++) {
          Event& event = schedule.events[eventIdx];
          
          // Parse event time
          int eventHour = 0, eventMinute = 0;
          if (sscanf(event.time.c_str(), "%d:%d", &eventHour, &eventMinute) != 2) {
            debugPrintf("  WARNING: Invalid time format in event: %s\n", event.time.c_str());
            continue;
          }
          
          // Calculate event time in minutes since midnight
          int eventMinutes = eventHour * 60 + eventMinute;
          
          // Calculate time until this event
          int minutesUntilEvent;
          if (eventMinutes > currentMinutes) {
            // Event is later today
            minutesUntilEvent = eventMinutes - currentMinutes;
          } else {
            // Event is tomorrow
            minutesUntilEvent = (24 * 60) - currentMinutes + eventMinutes;
          }
          
          // Check if this event should have executed already today
          bool shouldHaveExecuted = (eventMinutes <= currentMinutes);
          bool wasExecuted = (event.executedMask & 0x01) != 0;
          
          // Print current time and event time for debugging
          debugPrintf("  DEBUG: Current time: %02d:%02d (%d minutes since midnight)\n", 
            timeinfo.tm_hour, timeinfo.tm_min, currentMinutes);
          debugPrintf("  DEBUG: Event time: %s (%d minutes since midnight)\n", 
            event.time.c_str(), eventMinutes);
          debugPrintf("  DEBUG: Minutes until event: %d\n", minutesUntilEvent);

          debugPrintf("  Event %d: Time %s (%d min), Duration %d sec, ID %s\n", 
                    eventIdx, event.time.c_str(), eventMinutes, event.duration, 
                    event.id.c_str());
          debugPrintf("    Minutes until execution: %d\n", minutesUntilEvent);
          debugPrintf("    Should have executed today: %s\n", shouldHaveExecuted ? "YES" : "NO");
          debugPrintf("    Was executed today: %s\n", wasExecuted ? "YES" : "NO");
          
          // Check if this is the earliest upcoming event
          if (!wasExecuted && minutesUntilEvent < (earliestEventTime / 60)) {
            earliestEventTime = minutesUntilEvent * 60;
            earliestEventId = event.id;
            earliestScheduleName = schedule.name;
            earliestScheduleRelayMask = schedule.relayMask;
          }
        }
      }
      
      // Display next event information
      if (earliestEventTime != INT32_MAX) {
        int hours = earliestEventTime / 3600;
        int minutes = (earliestEventTime % 3600) / 60;
        int seconds = earliestEventTime % 60;
        
        debugPrintln("\n----- NEXT SCHEDULED EVENT -----");
        debugPrintf("Next event: ID %s in schedule '%s'\n", 
                  earliestEventId.c_str(), earliestScheduleName.c_str());
        debugPrintf("Will execute in: %02d:%02d:%02d (HH:MM:SS)\n", 
                  hours, minutes, seconds);
        debugPrintf("Will activate relays: 0x%02X\n", earliestScheduleRelayMask);
        
        // Show which specific relays will be activated
        debugPrintln("Relays to activate: ");
        for (int i = 0; i < 8; i++) {
          if (earliestScheduleRelayMask & (1 << i)) {
            debugPrintf("%d ", i);
          }
        }
        debugPrintln("");
      } else {
        debugPrintln("\nNo upcoming events found or all events executed today");
      }
      
      // Add information about the current relay state
      debugPrintln("\n----- CURRENT RELAY STATE -----");
      debugPrintf("Current relay state: 0x%02X\n", getRelayState());
      debugPrintln("Active relays: ");
      uint8_t currentState = getRelayState();
      bool anyActive = false;
      for (int i = 0; i < 8; i++) {
        if (currentState & (1 << i)) {
          debugPrintf("%d ", i);
          anyActive = true;
        }
      }
      if (!anyActive) {
        debugPrintln("None");
      }
      debugPrintln("\n-------------------------------");
    } else {
      if (!schedulerActive) {
        debugPrintln("DEBUG: Scheduler is not active");
      }
      if (schedulerState.scheduleCount == 0) {
        debugPrintln("DEBUG: No schedules defined");
      }
    }
    
    // Run every 60 seconds
    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

// Enhanced debug function for scheduler events
void debugScheduleEvent(const Event& event, bool executed, int minutesUntil) {
  debugPrintf("EVENT: %s (ID: %s, Duration: %d sec)\n", 
             event.time.c_str(), event.id.c_str(), event.duration);
  debugPrintf("  Execution status: %s\n", executed ? "EXECUTED" : "PENDING");
  
  if (!executed) {
    if (minutesUntil > 0) {
      int hours = minutesUntil / 60;
      int mins = minutesUntil % 60;
      debugPrintf("  Will execute in: %02d:%02d (HH:MM)\n", hours, mins);
    } else {
      debugPrintln("  MISSED EXECUTION - Event should have run but didn't");
    }
  }
}

// Debug function to validate time values
bool validateTimeFormat(const char* timeStr) {
  int hour, minute;
  
  // Attempt to parse the time string
  int parsed = sscanf(timeStr, "%d:%d", &hour, &minute);
  
  // Check if parsing was successful and values are in valid ranges
  if (parsed != 2 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    debugPrintf("ERROR: Invalid time format '%s' (parsed=%d, hour=%d, minute=%d)\n", 
               timeStr, parsed, hour, minute);
    return false;
  }
  
  return true;
}

// Function to directly test relay control
void testRelayControl() {
  debugPrintln("DEBUG: Starting relay control test...");
  
  // Get the current relay state for reference
  uint8_t initialState = getRelayState();
  debugPrintf("DEBUG: Initial relay state: 0x%02X\n", initialState);
  
  // Test each relay individually
  for (int relay = 0; relay < 8; relay++) {
    debugPrintf("DEBUG: Testing relay %d: ON\n", relay);
    
    // Turn relay on
    setRelay(relay, true);
    
    // Verify relay state
    uint8_t newState = getRelayState();
    bool relayOn = (newState & (1 << relay)) != 0;
    
    debugPrintf("DEBUG: Relay %d state: %s (expected: ON)\n", 
               relay, relayOn ? "ON" : "OFF");
    
    // Wait 2 seconds
    delay(2000);
    
    // Turn relay off
    debugPrintf("DEBUG: Testing relay %d: OFF\n", relay);
    setRelay(relay, false);
    
    // Verify relay state
    newState = getRelayState();
    bool relayOff = (newState & (1 << relay)) == 0;
    
    debugPrintf("DEBUG: Relay %d state: %s (expected: OFF)\n", 
               relay, relayOff ? "OFF" : "ON");
    
    // Wait 1 second between relays
    delay(1000);
  }
  
  // Restore original state
  debugPrintf("DEBUG: Restoring initial relay state: 0x%02X\n", initialState);
  setAllRelays(initialState);
  
  debugPrintln("DEBUG: Relay control test complete");
}

// Function to verify time synchronization (critical for scheduler)
bool verifyTimeSync() {
  time_t now = time(NULL);
  struct tm timeinfo;
  
  // Check if we have valid time
  if (now < 0 || !getLocalTime(&timeinfo)) {
    debugPrintln("ERROR: System time not set! NTP sync may have failed.");
    return false;
  }
  
  // Format time for display
  char timeStr[32];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  debugPrintf("DEBUG: System time correctly set: %s\n", timeStr);
  
  // Check if time appears valid (after Jan 1, 2022)
  time_t minValidTime = 1640995200; // Jan 1, 2022 00:00:00 GMT
  if (now < minValidTime) {
    debugPrintln("ERROR: System time appears invalid (before 2022)");
    return false;
  }
  
  return true;
}

// Function to manually trigger an event (used for debugging)
void manuallyTriggerEvent(const char* scheduleName, const char* eventId) {
  debugPrintf("DEBUG: Manually triggering event '%s' in schedule '%s'\n", 
             eventId, scheduleName);
  
  // Find the specified schedule
  Schedule* targetSchedule = nullptr;
  Event* targetEvent = nullptr;
  
  for (int i = 0; i < schedulerState.scheduleCount; i++) {
    if (schedulerState.schedules[i].name == scheduleName) {
      targetSchedule = &schedulerState.schedules[i];
      break;
    }
  }
  
  if (!targetSchedule) {
    debugPrintf("ERROR: Schedule '%s' not found\n", scheduleName);
    return;
  }
  
  // Find the specified event
  for (int i = 0; i < targetSchedule->eventCount; i++) {
    if (targetSchedule->events[i].id == eventId) {
      targetEvent = &targetSchedule->events[i];
      break;
    }
  }
  
  if (!targetEvent) {
    debugPrintf("ERROR: Event '%s' not found in schedule '%s'\n", 
               eventId, scheduleName);
    return;
  }
  
  // Execute the event
  debugPrintf("DEBUG: Executing event at %s for %d seconds\n", 
             targetEvent->time.c_str(), targetEvent->duration);
  
  // Activate the relays specified by the schedule's relay mask
  for (int relay = 0; relay < 8; relay++) {
    if (targetSchedule->relayMask & (1 << relay)) {
      debugPrintf("DEBUG: Activating relay %d for %d seconds\n", 
                 relay, targetEvent->duration);
      executeRelayCommand(relay, targetEvent->duration);
    }
  }
}

// Automated diagnostics task for the scheduler
void schedulerDiagnosticsTask(void *pvParameters) {
  // Wait for system to boot up and stabilize
  vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds after boot
  
  debugPrintln("\n\n==== STARTING AUTOMATED SCHEDULER DIAGNOSTICS ====\n");
  
  // Step 1: Check time synchronization
  debugPrintln("DIAGNOSTIC: Checking time synchronization...");
  bool timeValid = verifyTimeSync();
  
  if (!timeValid) {
    debugPrintln("DIAGNOSTIC: ❌ TIME SYNC FAILURE - Scheduler cannot function without correct time");
    debugPrintln("DIAGNOSTIC: Recommendation: Check WiFi connection and NTP server access");
  } else {
    debugPrintln("DIAGNOSTIC: ✅ Time synchronization is working correctly");
  }
  
  // Step 2: Test relay control
  debugPrintln("\nDIAGNOSTIC: Testing direct relay control...");
  
  // Save current relay state
  uint8_t savedRelayState = getRelayState();
  
  // Test relay 0 only (to minimize disruption)
  debugPrintln("DIAGNOSTIC: Testing relay 0 only");
  setRelay(0, true);
  vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second
  
  // Check if relay was activated
  uint8_t newState = getRelayState();
  bool relay0On = (newState & 0x01) != 0;
  
  if (relay0On) {
    debugPrintln("DIAGNOSTIC: ✅ Direct relay control is working");
  } else {
    debugPrintln("DIAGNOSTIC: ❌ Direct relay control FAILED - Relay did not activate");
    debugPrintln("DIAGNOSTIC: Recommendation: Check IOManager.cpp and hardware connections");
  }
  
  // Turn relay off and restore original state
  setRelay(0, false);
  vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second
  setAllRelays(savedRelayState);
  
  // Step 3: Check scheduler activation status
  debugPrintln("\nDIAGNOSTIC: Checking scheduler activation status...");
  
  if (schedulerActive) {
    debugPrintln("DIAGNOSTIC: ✅ Scheduler is ACTIVE");
  } else {
    debugPrintln("DIAGNOSTIC: ❌ Scheduler is NOT ACTIVE - Events will not execute");
    debugPrintln("DIAGNOSTIC: Recommendation: Call startSchedulerTask() or activate via web interface");
    
    // Try to activate the scheduler
    debugPrintln("DIAGNOSTIC: Attempting to activate scheduler...");
    startSchedulerTask();
    
    vTaskDelay(pdMS_TO_TICKS(1000)); // Brief delay
    
    if (schedulerActive) {
      debugPrintln("DIAGNOSTIC: ✅ Successfully activated the scheduler");
    } else {
      debugPrintln("DIAGNOSTIC: ❌ Failed to activate scheduler");
    }
  }
  
  // Step 4: Check schedules and events
  debugPrintln("\nDIAGNOSTIC: Checking schedules and events...");
  bool foundActiveSchedule = false;
  int totalEvents = 0;

  if (schedulerState.scheduleCount == 0) {
    debugPrintln("DIAGNOSTIC: ❌ No schedules defined - Create at least one schedule");
  } else {
    debugPrintf("DIAGNOSTIC: Found %d schedules\n", schedulerState.scheduleCount);
    
    bool foundUpcomingEvent = false;
    
    for (int i = 0; i < schedulerState.scheduleCount; i++) {
      Schedule& schedule = schedulerState.schedules[i];
      
      debugPrintf("DIAGNOSTIC: Schedule '%s': Relay mask: 0x%02X, Event count: %d\n", 
                schedule.name.c_str(), schedule.relayMask, schedule.eventCount);
      
      if (schedule.relayMask == 0) {
        debugPrintf("DIAGNOSTIC: ⚠️ Schedule '%s' has no relays assigned (inactive)\n", 
                  schedule.name.c_str());
      } else {
        foundActiveSchedule = true;
      }
      
      totalEvents += schedule.eventCount;
      
      // Check for valid event times
      for (int j = 0; j < schedule.eventCount; j++) {
        Event& event = schedule.events[j];
        if (!validateTimeFormat(event.time.c_str())) {
          debugPrintf("DIAGNOSTIC: ❌ Invalid time format in event: '%s'\n", 
                    event.time.c_str());
        } else {
          foundUpcomingEvent = true;
        }
      }
    }
    
    if (!foundActiveSchedule) {
      debugPrintln("DIAGNOSTIC: ❌ No schedules have relays assigned - Events won't control any relays");
      debugPrintln("DIAGNOSTIC: Recommendation: Assign relays to at least one schedule");
    } else {
      debugPrintln("DIAGNOSTIC: ✅ Found active schedules with relay assignments");
    }
    
    if (totalEvents == 0) {
      debugPrintln("DIAGNOSTIC: ❌ No events defined in any schedule");
      debugPrintln("DIAGNOSTIC: Recommendation: Add at least one event to a schedule");
    } else if (!foundUpcomingEvent) {
      debugPrintln("DIAGNOSTIC: ❌ No valid upcoming events found");
    } else {
      debugPrintln("DIAGNOSTIC: ✅ Valid events found in schedules");
    }
  }
  
  // Step 5: Test event execution by creating a test event
  if (timeValid && foundActiveSchedule) {
    debugPrintln("\nDIAGNOSTIC: Testing event execution by creating a test event...");
    
    // Get current time
    time_t now = time(NULL);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    
    // Create a test event 2 minutes from now
    timeinfo.tm_min += 2;
    if (timeinfo.tm_min >= 60) {
      timeinfo.tm_min -= 60;
      timeinfo.tm_hour += 1;
      if (timeinfo.tm_hour >= 24) {
        timeinfo.tm_hour = 0;
      }
    }
    
    char testTime[6];
    sprintf(testTime, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    
    debugPrintf("DIAGNOSTIC: Created test event at %s UTC (2 minutes from now)\n", testTime);
    debugPrintln("DIAGNOSTIC: Monitor the serial output for the next few minutes");
    debugPrintln("DIAGNOSTIC: You should see the event execute automatically when the time is reached");
  }
  
  // Summary and recommendations
  debugPrintln("\n==== SCHEDULER DIAGNOSTICS SUMMARY ====");
  
  if (!timeValid) {
    debugPrintln("❌ CRITICAL: Time synchronization failure");
  }
  
  if (!relay0On) {
    debugPrintln("❌ CRITICAL: Relay control not working");
  }
  
  if (!schedulerActive) {
    debugPrintln("❌ CRITICAL: Scheduler is not active");
  }
  
  if (!foundActiveSchedule || totalEvents == 0) {
    debugPrintln("❌ CRITICAL: No active schedules or events");
  }
  
  if (timeValid && relay0On && schedulerActive && foundActiveSchedule && totalEvents > 0) {
    debugPrintln("✅ All critical components appear to be working correctly");
    debugPrintln("If events still don't execute, the issue may be with precise timing");
    debugPrintln("Keep watching the monitor output for detailed event execution logs");
  }
  
  debugPrintln("\n==== END OF DIAGNOSTICS ====\n");
  
  // Task complete, delete itself
  vTaskDelete(NULL);
}

// Add this function to directly execute the next event

void executeNextScheduledEvent() {
  debugPrintln("\n==== MANUALLY EXECUTING NEXT EVENT ====");
  
  // Get current time
  time_t now = time(NULL);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  
  // Format current time for debugging
  char currentTimeStr[32];
  strftime(currentTimeStr, sizeof(currentTimeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  debugPrintf("Current time (UTC): %s\n", currentTimeStr);
  
  // Find the next event
  Event* nextEvent = nullptr;
  Schedule* nextSchedule = nullptr;
  time_t soonestTime = INT32_MAX;
  
  // Search all schedules for the next event
  for (int scheduleIdx = 0; scheduleIdx < schedulerState.scheduleCount; scheduleIdx++) {
    Schedule& schedule = schedulerState.schedules[scheduleIdx];
    
    // Skip inactive schedules
    if (schedule.relayMask == 0) continue;
    
    for (int eventIdx = 0; eventIdx < schedule.eventCount; eventIdx++) {
      Event& event = schedule.events[eventIdx];
      
      // Parse event time
      int eventHour = 0, eventMinute = 0;
      if (sscanf(event.time.c_str(), "%d:%d", &eventHour, &eventMinute) != 2) {
        debugPrintf("Invalid time format in event: %s\n", event.time.c_str());
        continue;
      }
      
      int eventMinutes = eventHour * 60 + eventMinute;
      int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
      
      // Calculate time until this event
      int minutesUntilEvent;
      if (eventMinutes > currentMinutes) {
        minutesUntilEvent = eventMinutes - currentMinutes;
      } else {
        minutesUntilEvent = (24 * 60) - currentMinutes + eventMinutes;
      }
      
      time_t secondsUntilEvent = minutesUntilEvent * 60;
      
      // Check if this is the soonest event
      if (secondsUntilEvent < soonestTime) {
        soonestTime = secondsUntilEvent;
        nextEvent = &event;
        nextSchedule = &schedule;
      }
    }
  }
  
  // Execute the next event if found
  if (nextEvent && nextSchedule) {
    debugPrintf("Executing event at %s from schedule '%s'\n", 
               nextEvent->time.c_str(), nextSchedule->name.c_str());
    
    // Activate the relays in this schedule
    for (int relay = 0; relay < 8; relay++) {
      if (nextSchedule->relayMask & (1 << relay)) {
        debugPrintf("Activating relay %d for %d seconds\n", relay, nextEvent->duration);
        
        // Direct relay control instead of using executeRelayCommand
        setRelay(relay, true);
        
        // Create a task to turn off the relay after the duration
        int duration = nextEvent->duration;
        int relayNum = relay;
        
        // Use a lambda for the task
        xTaskCreatePinnedToCore(
          [](void* parameter) {
            // Extract parameters
            int* params = (int*)parameter;
            int relayNum = params[0];
            int duration = params[1];
            
            // Wait for the specified duration
            vTaskDelay(pdMS_TO_TICKS(duration * 1000));
            
            // Turn off the relay
            setRelay(relayNum, false);
            debugPrintf("Relay %d turned OFF after %d seconds\n", relayNum, duration);
            
            // Free the parameters and delete the task
            delete[] params;
            vTaskDelete(NULL);
          },
          "RelayOff",
          4096,
          new int[2]{relayNum, duration},
          2,
          NULL,
          1
        );
      }
    }
    
    // Mark the event as executed
    nextEvent->executedMask |= 0x01;
    
    debugPrintln("Event execution initiated successfully");
  } else {
    debugPrintln("No upcoming events found to execute");
  }
  
  debugPrintln("==== MANUAL EXECUTION COMPLETE ====\n");
}

// Task to force immediate execution for testing
void immediateExecutionTask(void *pvParameters) {
  // Wait 10 seconds to let the system boot up
  vTaskDelay(pdMS_TO_TICKS(10000));
  
  debugPrintln("\n==== FORCE IMMEDIATE EXECUTION TASK STARTED ====");
  
  // Make sure scheduler is active
  if (!schedulerActive) {
    debugPrintln("Activating scheduler...");
    startSchedulerTask();
  }
  
  // Force a time check
  time_t now = time(NULL);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  
  char timeStr[32];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
  debugPrintf("Current system time: %s\n", timeStr);
  
  // Execute the next scheduled event directly
  executeNextScheduledEvent();
  
  // Schedule another execution in 3 minutes
  vTaskDelay(pdMS_TO_TICKS(180000)); // 3 minutes
  debugPrintln("\nExecuting another event after 3 minutes...");
  executeNextScheduledEvent();
  
  // Delete this task when done
  vTaskDelete(NULL);
}

void testTimeConversion() {
  debugPrintln("\n===== TIME CONVERSION TEST =====");
  
  // Get current time
  time_t now = time(NULL);
  struct tm localTime, utcTime;
  localtime_r(&now, &localTime);
  gmtime_r(&now, &utcTime);
  
  // Format for display
  char localTimeStr[20], utcTimeStr[20];
  strftime(localTimeStr, sizeof(localTimeStr), "%H:%M:%S", &localTime);
  strftime(utcTimeStr, sizeof(utcTimeStr), "%H:%M:%S", &utcTime);
  
  debugPrintf("Current local time: %s\n", localTimeStr);
  debugPrintf("Current UTC time: %s\n", utcTimeStr);
  
  // Calculate offset
  int offsetHours = localTime.tm_hour - utcTime.tm_hour;
  if (offsetHours > 12) offsetHours -= 24;
  if (offsetHours < -12) offsetHours += 24;
  
  debugPrintf("Time zone offset: UTC%+d\n", offsetHours);
  
  // Test the conversion functions
  String testTimes[] = {"00:00", "06:00", "12:00", "18:00", "23:59"};
  
  for (const String& time : testTimes) {
    String utc = localTimeToUTC(time);
    String back = utcToLocalTime(utc);
    
    debugPrintf("Local %s -> UTC %s -> Local %s\n", 
               time.c_str(), utc.c_str(), back.c_str());
    
    // Verify round-trip conversion
    if (time != back) {
      debugPrintf("ERROR: Round-trip conversion failed for %s\n", time.c_str());
    }
  }
  
  debugPrintln("==============================\n");
}