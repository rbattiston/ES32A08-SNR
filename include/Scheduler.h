#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#define SCHEDULER_FILE "/scheduler.json"
#define MAX_EVENTS 20

struct LightSchedule {
  String lightsOnTime;
  String lightsOffTime;
};

struct Event {
  String id;
  String time;           // Original HH:MM string from user
  uint16_t startMinute;  // Computed minutes from midnight (derived from time)
  uint16_t duration;     // in seconds
  uint8_t relay;         // relay number (0–7)
  uint8_t repeatCount;   // number of extra occurrences (0 means one occurrence)
  uint16_t repeatInterval; // interval in minutes between occurrences
  uint32_t executedMask; // transient: bitmask tracking which occurrences fired
};

struct SchedulerState {
  LightSchedule lightSchedule;
  Event events[MAX_EVENTS];
  uint8_t eventCount;
};

extern SchedulerState schedulerState;

// Scheduler control functions
void initScheduler();
void startSchedulerTask();
void stopSchedulerTask();
void executeRelayCommand(uint8_t relay, uint16_t duration);

// Persistence functions
void loadSchedulerState();
void saveSchedulerState();

// API handlers
void handleLoadSchedulerState(AsyncWebServerRequest *request);
void handleSchedulerStatus(AsyncWebServerRequest *request);
void handleActivateScheduler(AsyncWebServerRequest *request);
void handleDeactivateScheduler(AsyncWebServerRequest *request);
void handleManualWatering(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // SCHEDULER_H
