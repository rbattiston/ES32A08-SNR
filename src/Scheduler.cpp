#include "Scheduler.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include "IOManager.h"

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

// Forward declarations
void checkAndExecuteScheduledEvents();

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