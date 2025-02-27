#include "Scheduler.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include "IOManager.h"  // Assumed to contain setRelay() or similar relay functions

// Global state instance.
SchedulerState schedulerState;

// Scheduler task handle.
static TaskHandle_t schedulerTaskHandle = NULL;

// Forward declaration of the scheduler task function.
static void schedulerTask(void *parameter);

// ------------------------------------------------------------
// Relay execution helper.
// This spawns a task to activate a relay for the specified duration
// without blocking the main scheduler loop.
struct RelayTaskParams {
  uint8_t relay;
  uint16_t duration; // in seconds
};

static void relayTask(void *param) {
  RelayTaskParams *p = (RelayTaskParams*) param;
  // Turn relay on (replace with your relay control code)
  setRelay(p->relay, true);
  // Wait for the specified duration
  vTaskDelay(pdMS_TO_TICKS(p->duration * 1000));
  // Turn relay off
  setRelay(p->relay, false);
  delete p;
  vTaskDelete(NULL);
}

void executeRelayCommand(uint8_t relay, uint16_t duration) {
  // Allocate parameters for the relay task.
  RelayTaskParams *p = new RelayTaskParams;
  p->relay = relay;
  p->duration = duration;
  // Create a new task for relay activation.
  xTaskCreatePinnedToCore(relayTask, "RelayTask", 2048, p, 1, NULL, 1);
}

// ------------------------------------------------------------
// Scheduler task: checks every second for events that need to fire.
static void schedulerTask(void *parameter) {
  time_t now;
  struct tm timeinfo;
  uint16_t currentDay = 0xFFFF; // initialize to an impossible day-of-year
  while (true) {
    if (getLocalTime(&timeinfo)) {
      now = time(NULL);
      // Calculate seconds since midnight.
      uint32_t secondsSinceMidnight = timeinfo.tm_hour * 3600UL + timeinfo.tm_min * 60UL + timeinfo.tm_sec;
      uint16_t today = timeinfo.tm_yday; // day of year

      // If a new day has begun, reset execution flags.
      if (today != currentDay) {
        for (uint8_t i = 0; i < schedulerState.eventCount; i++) {
          schedulerState.events[i].executedMask = 0;
        }
        currentDay = today;
      }

      // Iterate over each scheduled event.
      for (uint8_t i = 0; i < schedulerState.eventCount; i++) {
        Event &e = schedulerState.events[i];
        uint32_t initialTime = (uint32_t)e.startMinute * 60;  // in seconds
        uint8_t totalOccurrences = e.repeatCount + 1;
        // Compute the interval between occurrences.
        uint32_t interval = (totalOccurrences > 1) ? ((86400UL - initialTime) / totalOccurrences) : 0;

        // Check each occurrence.
        for (uint8_t n = 0; n < totalOccurrences; n++) {
          uint32_t occurrenceTime = initialTime + n * interval;
          // If this occurrence has not yet been executed...
          if (!(e.executedMask & (1UL << n))) {
            // If current time is within a 1-second window of the scheduled occurrence.
            if (secondsSinceMidnight >= occurrenceTime && secondsSinceMidnight < occurrenceTime + 1) {
              // Trigger the event.
              executeRelayCommand(e.relay, e.duration);
              // Mark this occurrence as executed.
              e.executedMask |= (1UL << n);
            }
          }
        }
      }
    }
    // Delay for 1 second.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ------------------------------------------------------------
// Initializes time (via NTP), loads scheduler state, and starts the scheduler task.
void initScheduler() {
  // Configure time (using NTP servers).
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  // Wait for time to be set.
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 5) {
    delay(1000);
    retry++;
  }
  // Set default light schedule if not loaded.
  schedulerState.lightSchedule.lightsOnTime = "06:00";
  schedulerState.lightSchedule.lightsOffTime = "18:00";
  schedulerState.eventCount = 0;

  // Load scheduler state from SPIFFS.
  loadSchedulerState();
  // Start the scheduler task.
  startSchedulerTask();
}

// ------------------------------------------------------------
// Starts the scheduler task if not already running.
void startSchedulerTask() {
  if (schedulerTaskHandle == NULL) {
    xTaskCreatePinnedToCore(schedulerTask, "SchedulerTask", 4096, NULL, 1, &schedulerTaskHandle, 1);
  }
}

// Stops the scheduler task.
void stopSchedulerTask() {
  if (schedulerTaskHandle != NULL) {
    vTaskDelete(schedulerTaskHandle);
    schedulerTaskHandle = NULL;
  }
}

// ------------------------------------------------------------
// Loads the scheduler state from SPIFFS (JSON format).
void loadSchedulerState() {
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_READ);
  if (!file) {
    // No file exists yet; use defaults.
    return;
  }
  size_t size = file.size();
  if (size == 0) {
    file.close();
    return;
  }
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    // JSON error; ignore and use defaults.
    return;
  }
  // Load light schedule.
  JsonObject ls = doc["lightSchedule"];
  schedulerState.lightSchedule.lightsOnTime = ls["lightsOnTime"] | "06:00";
  schedulerState.lightSchedule.lightsOffTime = ls["lightsOffTime"] | "18:00";

  // Load events.
  JsonArray events = doc["events"].as<JsonArray>();
  schedulerState.eventCount = 0;
  for (JsonObject evt : events) {
    if (schedulerState.eventCount < MAX_EVENTS) {
      Event &e = schedulerState.events[schedulerState.eventCount];
      e.id = evt["id"].as<String>();
      String timeStr = evt["time"].as<String>(); // expected format "HH:MM"
      int hour = 0, minute = 0;
      sscanf(timeStr.c_str(), "%d:%d", &hour, &minute);
      e.startMinute = hour * 60 + minute;
      e.duration = evt["duration"].as<uint16_t>();
      e.relay = evt["relay"].as<uint8_t>();
      e.repeatCount = evt["repeat"].as<uint8_t>();
      e.executedMask = 0;
      schedulerState.eventCount++;
    }
  }
}

// ------------------------------------------------------------
// Saves the scheduler state to SPIFFS (JSON format).
void saveSchedulerState() {
  DynamicJsonDocument doc(2048);
  JsonObject ls = doc.createNestedObject("lightSchedule");
  ls["lightsOnTime"] = schedulerState.lightSchedule.lightsOnTime;
  ls["lightsOffTime"] = schedulerState.lightSchedule.lightsOffTime;

  JsonArray events = doc.createNestedArray("events");
  for (uint8_t i = 0; i < schedulerState.eventCount; i++) {
    JsonObject evt = events.createNestedObject();
    evt["id"] = schedulerState.events[i].id;
    // Convert startMinute (minutes since midnight) to HH:MM string.
    uint16_t minutes = schedulerState.events[i].startMinute;
    uint8_t hour = minutes / 60;
    uint8_t minute = minutes % 60;
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, minute);
    evt["time"] = timeStr;
    evt["duration"] = schedulerState.events[i].duration;
    evt["relay"] = schedulerState.events[i].relay;
    evt["repeat"] = schedulerState.events[i].repeatCount;
  }
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_WRITE);
  if (!file) return;
  serializeJson(doc, file);
  file.close();
}

void handleLoadSchedulerState(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1024);
  JsonObject ls = doc.createNestedObject("lightSchedule");
  ls["lightsOnTime"] = schedulerState.lightSchedule.lightsOnTime;
  ls["lightsOffTime"] = schedulerState.lightSchedule.lightsOffTime;
  
  JsonArray events = doc.createNestedArray("events");
  for (uint8_t i = 0; i < schedulerState.eventCount; i++) {
    JsonObject evt = events.createNestedObject();
    evt["id"] = schedulerState.events[i].id;
    // Convert startMinute to "HH:MM" format.
    uint16_t minutes = schedulerState.events[i].startMinute;
    uint8_t hour = minutes / 60;
    uint8_t minute = minutes % 60;
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, minute);
    evt["time"] = timeStr;
    evt["duration"] = schedulerState.events[i].duration;
    evt["relay"] = schedulerState.events[i].relay;
    evt["repeat"] = schedulerState.events[i].repeatCount;
  }
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

// ----------------------------------------------------------------
// API Handler: Return basic status about the scheduler.
void handleSchedulerStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(512);
  // Check if the scheduler task is running.
  doc["isActive"] = (schedulerTaskHandle != NULL);
  doc["eventCount"] = schedulerState.eventCount;
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

// ----------------------------------------------------------------
// API Handler: Activate the scheduler.
void handleActivateScheduler(AsyncWebServerRequest *request) {
  startSchedulerTask();
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler activated\"}");
}

// ----------------------------------------------------------------
// API Handler: Deactivate the scheduler.
void handleDeactivateScheduler(AsyncWebServerRequest *request) {
  stopSchedulerTask();
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler deactivated\"}");
}

// ----------------------------------------------------------------
// API Handler: Execute manual watering (trigger a relay for a duration).
void handleManualWatering(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (len == 0) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data received\"}");
    return;
  }
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  if (error) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  if (doc.containsKey("relay") && doc.containsKey("duration")) {
    int relay = doc["relay"].as<int>();
    int duration = doc["duration"].as<int>();
    executeRelayCommand(relay, duration);
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Manual watering executed\"}");
  } else {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing relay or duration\"}");
  }
}