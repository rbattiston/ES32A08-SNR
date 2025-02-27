#ifndef SIMPLE_SCHEDULER_H
#define SIMPLE_SCHEDULER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// File used for persistence.
#define SCHEDULER_FILE "/scheduler.json"

// Maximum number of events.
#define MAX_EVENTS 20

// Structure for informational light schedule (not used for triggering)
struct LightSchedule {
  String lightsOnTime;  // e.g. "06:00"
  String lightsOffTime; // e.g. "18:00"
};

// Simplified event structure.
struct Event {
  String id;
  uint16_t startMinute;  // minutes from midnight
  uint16_t duration;     // in seconds
  uint8_t relay;         // relay number (0–7)
  uint8_t repeatCount;   // extra repetitions (0 means only once)
  uint32_t executedMask; // transient flag mask for executed occurrences
};

// Global scheduler state.
struct SchedulerState {
  LightSchedule lightSchedule;
  Event events[MAX_EVENTS];
  uint8_t eventCount;
};

extern SchedulerState schedulerState;

// Initializes time, loads scheduler state, and starts the scheduler task.
void initScheduler();

// Starts/stops the scheduler task.
void startSchedulerTask();
void stopSchedulerTask();

// Loads and saves scheduler state from/to SPIFFS.
void loadSchedulerState();
void saveSchedulerState();

// (This function is assumed to control the relay hardware.)
void executeRelayCommand(uint8_t relay, uint16_t duration);

// --- New API handler declarations ---
void handleLoadSchedulerState(AsyncWebServerRequest *request);
void handleSchedulerStatus(AsyncWebServerRequest *request);
void handleActivateScheduler(AsyncWebServerRequest *request);
void handleDeactivateScheduler(AsyncWebServerRequest *request);
void handleManualWatering(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // SIMPLE_SCHEDULER_H
