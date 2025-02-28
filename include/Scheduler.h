#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#define SCHEDULER_FILE "/scheduler.json"
#define MAX_EVENTS 50
#define MAX_SCHEDULES 8

// Individual event: times are stored in GMT "HH:MM"
struct Event {
  String id;
  String time;         // GMT "HH:MM"
  uint16_t startMinute; // computed minutes from midnight (GMT)
  uint16_t duration;   // seconds
  uint32_t executedMask; // transient flag (unused now, as each event is independent)
};

// A complete schedule for a 24‑hour period
struct Schedule {
  String name;         // Schedule name
  String metadata;     // e.g., save date/time
  uint8_t relayMask;   // bitmask for relays this schedule controls
  String lightsOnTime; // GMT "HH:MM"
  String lightsOffTime;// GMT "HH:MM"
  Event events[MAX_EVENTS];
  uint8_t eventCount;
};

// Global scheduler state
struct SchedulerState {
  Schedule schedules[MAX_SCHEDULES];
  uint8_t scheduleCount;
  uint8_t currentScheduleIndex; // Active schedule index
};

extern SchedulerState schedulerState;

// Function declarations
void initScheduler();
void startSchedulerTask();
void stopSchedulerTask();
void executeRelayCommand(uint8_t relay, uint16_t duration);
void loadSchedulerState();
void saveSchedulerState();

// API handlers…
void handleLoadSchedulerState(AsyncWebServerRequest *request);
void handleSchedulerStatus(AsyncWebServerRequest *request);
void handleActivateScheduler(AsyncWebServerRequest *request);
void handleDeactivateScheduler(AsyncWebServerRequest *request);
void handleManualWatering(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // SCHEDULER_H
