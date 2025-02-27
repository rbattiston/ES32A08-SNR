#include "Scheduler.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include "IOManager.h"  // Assumed to contain setRelay()

// Define debug functions if not provided elsewhere.
#ifndef debugPrintln
  #define debugPrintln(msg) Serial.println(msg)
#endif
#ifndef debugPrintf
  #define debugPrintf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#endif

// Global state instance.
SchedulerState schedulerState;

// Scheduler task handle.
static TaskHandle_t schedulerTaskHandle = NULL;

// Forward declaration of the scheduler task function.
static void schedulerTask(void *parameter);

// ------------------------------------------------------------
// Relay execution helper.
struct RelayTaskParams {
  uint8_t relay;
  uint16_t duration; // in seconds
};

static void relayTask(void *param) {
  RelayTaskParams *p = (RelayTaskParams*) param;
  setRelay(p->relay, true);
  vTaskDelay(pdMS_TO_TICKS(p->duration * 1000));
  setRelay(p->relay, false);
  delete p;
  vTaskDelete(NULL);
}

uint32_t secondsSinceMidnight = time(NULL) % 86400;

void executeRelayCommand(uint8_t relay, uint16_t duration) {
  debugPrintf("DEBUG: Executing relay command: relay %d, duration %d seconds\n", relay, duration);
  RelayTaskParams *p = new RelayTaskParams;
  p->relay = relay;
  p->duration = duration;
  xTaskCreatePinnedToCore(relayTask, "RelayTask", 2048, p, 1, NULL, 1);
}

// ------------------------------------------------------------
// Scheduler task: checks every second for events that need to fire.
static void schedulerTask(void *parameter) {
  uint16_t currentDay = 0xFFFF; // initialize to an impossible day-of-year
  while (true) {
    // Use getLocalTime() only for printing if needed, but use time(NULL) for calculations.
    time_t now = time(NULL);
    uint32_t secondsSinceMidnight = now % 86400; // current GMT seconds since midnight
    
    // Optionally, for debugging, convert now to a tm in GMT:
    struct tm *gmTime = gmtime(&now);
    char timeStr[9];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", gmTime);
    debugPrintf("DEBUG: Current GMT time: %s\n", timeStr);
    
    // For day change detection, we can use now / 86400 (number of days since epoch)
    uint16_t today = now / 86400;
    if (today != currentDay) {
      for (uint8_t i = 0; i < schedulerState.eventCount; i++) {
        schedulerState.events[i].executedMask = 0;
      }
      currentDay = today;
      debugPrintln("DEBUG: New day detected; resetting executedMask for all events");
    }
    
    // Iterate over each event.
    for (uint8_t i = 0; i < schedulerState.eventCount; i++) {
      Event &e = schedulerState.events[i];
      // e.startMinute now represents the GMT minutes (since the event was stored in GMT)
      uint32_t initialTime = (uint32_t)e.startMinute * 60;  // in seconds
      uint8_t totalOccurrences = e.repeatCount + 1;
      uint32_t interval = (totalOccurrences > 1) ? (e.repeatInterval * 60UL) : 0; // in seconds
      for (uint8_t n = 0; n < totalOccurrences; n++) {
        uint32_t occurrenceTime = initialTime + n * interval;
        debugPrintf("DEBUG: Checking event %d occurrence %d: occurrenceTime=%lu, current=%lu\n", i, n, occurrenceTime, secondsSinceMidnight);
        // For testing, widen window to 2 seconds
        if (!(e.executedMask & (1UL << n)) && (secondsSinceMidnight >= occurrenceTime && secondsSinceMidnight < occurrenceTime + 2)) {
          debugPrintf("DEBUG: Firing event %d occurrence %d: Relay %d for %d seconds\n", i, n, e.relay, e.duration);
          executeRelayCommand(e.relay, e.duration);
          e.executedMask |= (1UL << n);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ------------------------------------------------------------
// Initializes time (via NTP), loads scheduler state, and starts the scheduler task.
void initScheduler() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 5) {
    delay(1000);
    retry++;
  }
  schedulerState.lightSchedule.lightsOnTime = "06:00";
  schedulerState.lightSchedule.lightsOffTime = "18:00";
  schedulerState.eventCount = 0;
  loadSchedulerState();
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
    debugPrintln("DEBUG: scheduler.json not found, using defaults");
    return;
  }
  size_t size = file.size();
  debugPrintf("DEBUG: scheduler.json size: %d bytes\n", size);
  if (size == 0) {
    debugPrintln("DEBUG: scheduler.json is empty");
    file.close();
    return;
  }
  
  String rawContent;
  while(file.available()){
    rawContent += (char)file.read();
  }
  debugPrintln("DEBUG: Raw scheduler.json content: " + rawContent);
  
  file.seek(0, SeekSet);
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    return;
  }
  
  JsonObject ls = doc["lightSchedule"];
  schedulerState.lightSchedule.lightsOnTime = ls["lightsOnTime"] | "06:00";
  schedulerState.lightSchedule.lightsOffTime = ls["lightsOffTime"] | "18:00";
  debugPrintf("DEBUG: Loaded light schedule: on=%s, off=%s\n", 
              schedulerState.lightSchedule.lightsOnTime.c_str(), 
              schedulerState.lightSchedule.lightsOffTime.c_str());
  
  JsonArray events = doc["events"].as<JsonArray>();
  schedulerState.eventCount = 0;
  for (JsonObject evt : events) {
    if (schedulerState.eventCount < MAX_EVENTS) {
      Event &e = schedulerState.events[schedulerState.eventCount];
      e.id = evt["id"].as<String>();
      String timeStr = evt["time"].as<String>(); // expected format "HH:MM"
      e.time = timeStr;
      int hour = 0, minute = 0;
      sscanf(timeStr.c_str(), "%d:%d", &hour, &minute);
      e.startMinute = hour * 60 + minute;
      e.duration = evt["duration"].as<uint16_t>();
      e.relay = evt["relay"].as<uint8_t>();
      // Check for "repeatCount" key
      if (evt.containsKey("repeatCount"))
        e.repeatCount = evt["repeatCount"].as<uint8_t>();
      else
        e.repeatCount = 0;
      e.repeatInterval = evt["repeatInterval"].as<uint16_t>();
      e.executedMask = 0;
      debugPrintf("DEBUG: Loaded event: id=%s, time=%s, duration=%d, relay=%d, repeatCount=%d, interval=%d\n",
                  e.id.c_str(), e.time.c_str(), e.duration, e.relay, e.repeatCount, e.repeatInterval);
      schedulerState.eventCount++;
    }
  }
}


// ------------------------------------------------------------
// Saves the scheduler state to SPIFFS (JSON format).
void saveSchedulerState() {
  debugPrintln("DEBUG: Saving scheduler state to SPIFFS");
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_WRITE);
  if (!file) {
    debugPrintln("DEBUG: Failed to open scheduler file for writing");
    return;
  }
  
  DynamicJsonDocument doc(2048);
  JsonObject ls = doc.createNestedObject("lightSchedule");
  ls["lightsOnTime"] = schedulerState.lightSchedule.lightsOnTime;
  ls["lightsOffTime"] = schedulerState.lightSchedule.lightsOffTime;
  
  JsonArray events = doc.createNestedArray("events");
  for (uint8_t i = 0; i < schedulerState.eventCount; i++) {
    JsonObject evt = events.createNestedObject();
    evt["id"] = schedulerState.events[i].id;
    evt["time"] = schedulerState.events[i].time;
    evt["duration"] = schedulerState.events[i].duration;
    evt["relay"] = schedulerState.events[i].relay;
    evt["repeatCount"] = schedulerState.events[i].repeatCount;  // changed key here
    evt["repeatInterval"] = schedulerState.events[i].repeatInterval;
  }
  
  if (serializeJson(doc, file) == 0) {
    debugPrintln("DEBUG: Failed to write to scheduler file");
  }
  file.close();
  
  // Verify by re-opening the file
  File verifyFile = SPIFFS.open(SCHEDULER_FILE, FILE_READ);
  if (!verifyFile) {
    debugPrintln("DEBUG: Failed to re-open scheduler file for verification");
    return;
  }
  size_t size = verifyFile.size();
  debugPrintf("DEBUG: scheduler.json size after save: %d bytes\n", size);
  String rawContent;
  while (verifyFile.available()){
    rawContent += (char)verifyFile.read();
  }
  debugPrintln("DEBUG: scheduler.json content:\n" + rawContent);
  verifyFile.close();
  
  debugPrintln("DEBUG: Scheduler state successfully written to SPIFFS");
}




// ----------------------------------------------------------------
// API Handler: Return the current scheduler state as JSON.
void handleLoadSchedulerState(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1024);
  JsonObject ls = doc.createNestedObject("lightSchedule");
  ls["lightsOnTime"] = schedulerState.lightSchedule.lightsOnTime;
  ls["lightsOffTime"] = schedulerState.lightSchedule.lightsOffTime;
  
  JsonArray events = doc.createNestedArray("events");
  for (uint8_t i = 0; i < schedulerState.eventCount; i++) {
    JsonObject evt = events.createNestedObject();
    evt["id"] = schedulerState.events[i].id;
    // Convert startMinute back to "HH:MM" format using the stored string.
    evt["time"] = schedulerState.events[i].time;
    evt["duration"] = schedulerState.events[i].duration;
    evt["relay"] = schedulerState.events[i].relay;
    evt["repeat"] = schedulerState.events[i].repeatCount;
    evt["repeatInterval"] = schedulerState.events[i].repeatInterval;
  }
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

// ----------------------------------------------------------------
// API Handler: Return basic status about the scheduler.
void handleSchedulerStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(512);
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
// API Handler: Execute manual watering.
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
