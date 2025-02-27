// Scheduler.cpp
#include "Scheduler.h"
#include "Utils.h"
#include "IOManager.h"
#include "WebServer.h" // Make sure this is included
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>

// Global scheduler state
SchedulerState schedulerState;

// Mutex for scheduler access
portMUX_TYPE schedulerMutex = portMUX_INITIALIZER_UNLOCKED;

// Handle for the scheduler task
TaskHandle_t schedulerTaskHandle = NULL;

void initScheduler() {
  debugPrintln("DEBUG: Initializing scheduler...");
  
  // Initialize time
  initSchedulerTime();
  
  // Load scheduler state from file
  loadSchedulerState();
  
  // API endpoints are now set up in WebServer.cpp
  // So we don't need to set them up here anymore
  
  debugPrintln("DEBUG: Scheduler initialized");
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
    // Scheduler.cpp (continued)
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
  uint8_t oldRelayState = getRelayState();
  setRelay(relay, true);
  
  debugPrintf("DEBUG: Relay state changed: 0x%02X -> 0x%02X\n", oldRelayState, getRelayState());
  
  // Wait for duration
  delay(duration * 1000);
  
  // Clear relay bit
  oldRelayState = getRelayState();
  setRelay(relay, false);
  
  debugPrintf("DEBUG: Relay state changed: 0x%02X -> 0x%02X\n", oldRelayState, getRelayState());
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
  
  // Rest of the parsing code follows similar pattern...
  // (truncated for brevity)
  
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
  
// Continuing the handleLoadSchedulerState function
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