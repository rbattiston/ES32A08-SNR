#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWebServer.h>

// Scheduler configuration
#define SCHEDULER_FILE "/scheduler.json"
#define MAX_EVENTS 50
#define MAX_SCHEDULES 8
#define SCHEDULER_TIMEOUT_MS 300000  // 5 minutes (300,000 ms)

// Declare the WebSocket as external so it can be used across files
extern AsyncWebSocket schedulerWs;

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

// Enum for state management
enum SchedulerMode {
  MODE_VIEW_ONLY,
  MODE_CREATING,
  MODE_EDITING
};

// Struct for tracking editing sessions
struct EditSession {
  String sessionId;
  unsigned long lastActivity;
  SchedulerMode mode;
  int editingScheduleIndex;  // -1 for new schedule
  Schedule pendingSchedule;  // Temporary schedule being edited
  bool isDirty;              // Whether changes have been made
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
void schedulerMonitorTask(void *pvParameters);
bool verifyTimeSync();
void testRelayControl();
void manuallyTriggerEvent(const char* scheduleName, const char* eventId);
bool validateTimeFormat(const char* timeStr);
void debugScheduleEvent(const Event& event, bool executed, int minutesUntil);
void executeNextScheduledEvent();
void immediateExecutionTask(void *pvParameters);
void testTimeConversion();

// Automated scheduler diagnostics
void schedulerDiagnosticsTask(void *pvParameters);

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

// WebSocket handlers
void initSchedulerWebSocket(AsyncWebServer& server);
void handleWebSocketMessage(AsyncWebSocket* webSocket, AsyncWebSocketClient* client, 
                          AwsFrameInfo* info, uint8_t* data, size_t len);
void handleWebSocketEvent(AsyncWebSocket* webSocket, AsyncWebSocketClient* client, 
                        AwsEventType type, void* arg, uint8_t* data, size_t len);
void checkSchedulerTimeouts();

#endif // SCHEDULER_H