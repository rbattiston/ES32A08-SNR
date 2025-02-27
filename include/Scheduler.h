// Scheduler.h
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "TestMode.h"
#include "WebServer.h"  // This includes the reference to server

// Initialize scheduler
void initScheduler();

// Scheduler control functions
void startSchedulerTask();
void stopSchedulerTask();
void executeWatering(int relay, int duration);
void calculateNextEvent();
void saveSchedulerState();
void loadSchedulerState();
void initSchedulerTime();

// Task functions
void schedulerTask(void *parameter);
void manualWateringTask(void *parameter);

// API handlers
void handleSaveSchedulerState(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleLoadSchedulerState(AsyncWebServerRequest *request);
void handleSchedulerStatus(AsyncWebServerRequest *request);
void handleActivateScheduler(AsyncWebServerRequest *request);
void handleDeactivateScheduler(AsyncWebServerRequest *request);
void handleManualWatering(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// Constants
#define SCHEDULER_FILE "/scheduler.json"
#define MAX_SCHEDULES 50
#define MAX_EVENTS 20
#define MAX_TEMPLATES 10

// Helper structure for manual watering task parameters
struct ManualWateringParams {
  int relay;
  int duration;
};

// Scheduler state structure - matches the JavaScript state object
struct SchedulerState {
  struct {
    String lightsOnTime;
    String lightsOffTime;
  } lightSchedule;
  
  struct Schedule {
    String id;
    int frequency;    // In minutes
    int duration;     // In seconds
    int relay;        // Relay number (0-7)
  };
  
  struct Event {
    String id;
    String time;      // HH:MM format
    int duration;     // In seconds
    int relay;        // Relay number (0-7)
  };
  
  struct Template {
    String id;
    String name;
    String lightsOnTime;
    String lightsOffTime;
    Schedule lightsOnSchedules[MAX_SCHEDULES];
    int lightsOnSchedulesCount;
    Schedule lightsOffSchedules[MAX_SCHEDULES];
    int lightsOffSchedulesCount;
    Event customEvents[MAX_EVENTS];
    int customEventsCount;
  };
  
  Schedule lightsOnSchedules[MAX_SCHEDULES];
  int lightsOnSchedulesCount;
  
  Schedule lightsOffSchedules[MAX_SCHEDULES];
  int lightsOffSchedulesCount;
  
  Event customEvents[MAX_EVENTS];
  int customEventsCount;
  
  Template templates[MAX_TEMPLATES];
  int templatesCount;
  
  bool isActive;
  String currentLightCondition;  // "Lights On", "Lights Off", or "Unknown"
  
  struct NextEvent {
    String time;
    int duration;
    int relay;
  } nextEvent;
  
  bool hasNextEvent;
};

// External variables
extern SchedulerState schedulerState;
extern portMUX_TYPE schedulerMutex;
extern TaskHandle_t schedulerTaskHandle;

#endif // SCHEDULER_H