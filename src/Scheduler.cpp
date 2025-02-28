#include "Scheduler.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include "IOManager.h"  // Assumed to contain setRelay()

#ifndef debugPrintln
  #define debugPrintln(msg) Serial.println(msg)
#endif
#ifndef debugPrintf
  #define debugPrintf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#endif

SchedulerState schedulerState;
static TaskHandle_t schedulerTaskHandle = NULL;

/*
  Helper function to convert a GMT time string ("HH:MM") into a local time string ("HH:MM").
  This calculates the offset between local and GMT using the current time.
*/
String convertGMTtoLocal(String gmtTime) {
  int hour, minute;
  sscanf(gmtTime.c_str(), "%d:%d", &hour, &minute);
  int totalMins = hour * 60 + minute;

  time_t now = time(NULL);
  struct tm localTime;
  struct tm gmtTimeStruct;
  localtime_r(&now, &localTime);
  gmtime_r(&now, &gmtTimeStruct);
  int offsetSecs = (localTime.tm_hour - gmtTimeStruct.tm_hour) * 3600 +
                   (localTime.tm_min - gmtTimeStruct.tm_min) * 60;
  int offsetMins = offsetSecs / 60;

  int localTotalMins = totalMins + offsetMins;
  localTotalMins = ((localTotalMins % 1440) + 1440) % 1440;
  int localHour = localTotalMins / 60;
  int localMin = localTotalMins % 60;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", localHour, localMin);
  return String(buf);
}

// Scheduler task: check current GMT time (seconds since midnight) and fire events in the active schedule.
static void schedulerTask(void *parameter) {
  uint32_t currentDay = 0xFFFFFFFF;
  while (true) {
    time_t now = time(NULL); // GMT time (epoch in seconds)
    uint32_t secondsSinceMidnight = now % 86400;
    uint32_t day = now / 86400;
    if (day != currentDay) {
      // Reset executed masks for active schedule
      if (schedulerState.scheduleCount > 0) {
        Schedule &sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
        for (uint8_t i = 0; i < sch.eventCount; i++) {
          sch.events[i].executedMask = 0;
        }
      }
      currentDay = day;
      debugPrintln("DEBUG: New day detected; resetting executed masks");
    }
    if (schedulerState.scheduleCount > 0) {
      Schedule &sch = schedulerState.schedules[schedulerState.currentScheduleIndex];
      for (uint8_t i = 0; i < sch.eventCount; i++) {
        Event &e = sch.events[i];
        // e.startMinute is computed in GMT
        uint32_t eventTime = (uint32_t)e.startMinute * 60;
        // For testing, use a 2-second window for triggering
        if (!(e.executedMask & 1) && (secondsSinceMidnight >= eventTime && secondsSinceMidnight < eventTime + 2)) {
          debugPrintf("DEBUG: Firing event %d: Relay mask 0x%02X, Duration %d seconds\n", i, sch.relayMask, e.duration);
          for (uint8_t r = 0; r < 8; r++) {
            if (sch.relayMask & (1 << r)) {
              executeRelayCommand(r, e.duration);
            }
          }
          e.executedMask |= 1;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void executeRelayCommand(uint8_t relay, uint16_t duration) {
  debugPrintf("DEBUG: Executing relay command: relay %d, duration %d seconds\n", relay, duration);
  struct RelayTaskParams {
    uint8_t relay;
    uint16_t duration;
  };
  RelayTaskParams *p = new RelayTaskParams;
  p->relay = relay;
  p->duration = duration;
  xTaskCreatePinnedToCore([](void *param) {
    RelayTaskParams *p = (RelayTaskParams *) param;
    setRelay(p->relay, true);
    vTaskDelay(pdMS_TO_TICKS(p->duration * 1000));
    setRelay(p->relay, false);
    delete p;
    vTaskDelete(NULL);
  }, "RelayTask", 2048, p, 1, NULL, 1);
}

void initScheduler() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 5) {
    delay(1000);
    retry++;
  }
  // If no schedules exist, create a default schedule.
  if (schedulerState.scheduleCount == 0) {
    schedulerState.scheduleCount = 1;
    schedulerState.currentScheduleIndex = 0;
    Schedule &sch = schedulerState.schedules[0];
    sch.name = String("Default ") + String(timeinfo.tm_year + 1900) + "-" + String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday);
    sch.metadata = String("Saved on ") + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min);
    sch.relayMask = 0xFF; // All relays
    // Set default lights on/off times in GMT (for example, 06:00 and 18:00 local converted to GMT)
    // For now, assume they are stored in GMT; conversion happens on client.
    sch.lightsOnTime = "06:00";
    sch.lightsOffTime = "18:00";
    sch.eventCount = 0;
  }
  loadSchedulerState();
  startSchedulerTask();
}

void startSchedulerTask() {
  if (schedulerTaskHandle == NULL) {
    xTaskCreatePinnedToCore(schedulerTask, "SchedulerTask", 4096, NULL, 1, &schedulerTaskHandle, 1);
  }
}

void stopSchedulerTask() {
  if (schedulerTaskHandle != NULL) {
    vTaskDelete(schedulerTaskHandle);
    schedulerTaskHandle = NULL;
  }
}

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
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    return;
  }
  schedulerState.scheduleCount = doc["scheduleCount"] | 0;
  schedulerState.currentScheduleIndex = doc["currentScheduleIndex"] | 0;
  JsonArray schedules = doc["schedules"].as<JsonArray>();
  schedulerState.scheduleCount = 0;
  for (JsonObject schObj : schedules) {
    if (schedulerState.scheduleCount < MAX_SCHEDULES) {
      Schedule &sch = schedulerState.schedules[schedulerState.scheduleCount];
      sch.name = schObj["name"].as<String>();
      sch.metadata = schObj["metadata"].as<String>();
      sch.relayMask = schObj["relayMask"].as<uint8_t>();
      sch.lightsOnTime = schObj["lightsOnTime"].as<String>();
      sch.lightsOffTime = schObj["lightsOffTime"].as<String>();
      JsonArray events = schObj["events"].as<JsonArray>();
      sch.eventCount = 0;
      for (JsonObject evt : events) {
        if (sch.eventCount < MAX_EVENTS) {
          Event &e = sch.events[sch.eventCount];
          e.id = evt["id"].as<String>();
          e.time = evt["time"].as<String>(); // stored in GMT
          int hour = 0, minute = 0;
          sscanf(e.time.c_str(), "%d:%d", &hour, &minute);
          e.startMinute = hour * 60 + minute;
          e.duration = evt["duration"].as<uint16_t>();
          e.executedMask = 0;
          sch.eventCount++;
        }
      }
      schedulerState.scheduleCount++;
    }
  }
  debugPrintf("DEBUG: Loaded %d schedules\n", schedulerState.scheduleCount);
}

void saveSchedulerState() {
  debugPrintln("DEBUG: Saving scheduler state to SPIFFS");
  DynamicJsonDocument doc(4096);
  doc["scheduleCount"] = schedulerState.scheduleCount;
  doc["currentScheduleIndex"] = schedulerState.currentScheduleIndex;
  JsonArray schedules = doc.createNestedArray("schedules");
  for (uint8_t i = 0; i < schedulerState.scheduleCount; i++) {
    JsonObject schObj = schedules.createNestedObject();
    schObj["name"] = schedulerState.schedules[i].name;
    schObj["metadata"] = schedulerState.schedules[i].metadata;
    schObj["relayMask"] = schedulerState.schedules[i].relayMask;
    schObj["lightsOnTime"] = schedulerState.schedules[i].lightsOnTime;
    schObj["lightsOffTime"] = schedulerState.schedules[i].lightsOffTime;
    JsonArray events = schObj.createNestedArray("events");
    Schedule &sch = schedulerState.schedules[i];
    for (uint8_t j = 0; j < sch.eventCount; j++) {
      JsonObject evt = events.createNestedObject();
      evt["id"] = sch.events[j].id;
      evt["time"] = sch.events[j].time;  // Stored in GMT
      evt["duration"] = sch.events[j].duration;
    }
  }
  File file = SPIFFS.open(SCHEDULER_FILE, FILE_WRITE);
  if (!file) {
    debugPrintln("DEBUG: Failed to open scheduler file for writing");
    return;
  }
  if (serializeJson(doc, file) == 0) {
    debugPrintln("DEBUG: Failed to write to scheduler file");
  }
  file.close();
  debugPrintln("DEBUG: Scheduler state successfully written to SPIFFS");
}

// API Handlers
void handleLoadSchedulerState(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(4096);
  doc["scheduleCount"] = schedulerState.scheduleCount;
  doc["currentScheduleIndex"] = schedulerState.currentScheduleIndex;
  JsonArray schedules = doc.createNestedArray("schedules");
  for (uint8_t i = 0; i < schedulerState.scheduleCount; i++) {
    JsonObject schObj = schedules.createNestedObject();
    schObj["name"] = schedulerState.schedules[i].name;
    schObj["metadata"] = schedulerState.schedules[i].metadata;
    schObj["relayMask"] = schedulerState.schedules[i].relayMask;
    // Convert stored GMT lights on/off times to local before sending
    schObj["lightsOnTime"] = convertGMTtoLocal(schedulerState.schedules[i].lightsOnTime);
    schObj["lightsOffTime"] = convertGMTtoLocal(schedulerState.schedules[i].lightsOffTime);
    JsonArray events = schObj.createNestedArray("events");
    Schedule &sch = schedulerState.schedules[i];
    for (uint8_t j = 0; j < sch.eventCount; j++) {
      JsonObject evt = events.createNestedObject();
      evt["id"] = sch.events[j].id;
      // Convert event time from GMT to local time for display
      evt["time"] = convertGMTtoLocal(sch.events[j].time);
      evt["duration"] = sch.events[j].duration;
    }
  }
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void handleSchedulerStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(512);
  doc["isActive"] = (schedulerTaskHandle != NULL);
  doc["scheduleCount"] = schedulerState.scheduleCount;
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void handleActivateScheduler(AsyncWebServerRequest *request) {
  startSchedulerTask();
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler activated\"}");
}

void handleDeactivateScheduler(AsyncWebServerRequest *request) {
  stopSchedulerTask();
  request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler deactivated\"}");
}

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
