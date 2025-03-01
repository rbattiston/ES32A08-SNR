#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Scheduler configuration
#define SCHEDULER_FILE "/scheduler.json"
#define MAX_EVENTS 50
#define MAX_SCHEDULES 8

// Event structure
// Each event represents a single activation at a specific time
struct Event {
  String id;             // Unique identifier for the event
  String time;           // Time in UTC format "HH:MM"
  uint16_t duration;     // Duration in seconds
  uint32_t executedMask; // Flags for tracking execution status
};

// Schedule structure
// A schedule contains metadata and a collection of events
struct Schedule {
  String name;           // User-defined name for the schedule
  String metadata;       // Additional information (e.g., creation date)
  uint8_t relayMask;     // Bitmask of relays controlled by this schedule
  String lightsOnTime;   // "Lights on" time (metadata) in UTC "HH:MM"
  String lightsOffTime;  // "Lights off" time (metadata) in UTC "HH:MM"
  Event events[MAX_EVENTS]; // Array of events
  uint8_t eventCount;    // Number of events in this schedule
};

// Global scheduler state
struct SchedulerState {
  Schedule schedules[MAX_SCHEDULES]; // Array of schedules
  uint8_t scheduleCount;             // Number of schedules
  uint8_t currentScheduleIndex;      // Index of currently selected schedule
};

// Global scheduler state instance
extern SchedulerState schedulerState;

// Core scheduler functions
void initScheduler();
void startSchedulerTask();
void stopSchedulerTask();
void executeRelayCommand(uint8_t relay, uint16_t duration);
void loadSchedulerState();
void saveSchedulerState();
void addNewSchedule(const String& name);

// Time conversion utilities
String localTimeToUTC(const String& localTime);
String utcToLocalTime(const String& utcTime);

// API handlers
void handleLoadSchedulerState(AsyncWebServerRequest *request);
void handleSaveSchedulerState(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleSchedulerStatus(AsyncWebServerRequest *request);
void handleActivateScheduler(AsyncWebServerRequest *request);
void handleDeactivateScheduler(AsyncWebServerRequest *request);
void handleManualWatering(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // SCHEDULER_H