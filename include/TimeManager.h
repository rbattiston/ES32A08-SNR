#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <time.h>
#include <ESPAsyncWebServer.h>

// Initializes the time manager (sets up NTP and starts the background task)
void initTimeManager();

// Starts a background task that monitors and re-syncs NTP if needed.
void startTimeManagerTask();

// Set the timezone
bool setTimezone(const char* tz);

// Get the current timezone
const char* getCurrentTimezone();

// Get the first sync time (when NTP first succeeded)
time_t getFirstSyncTime();

// Check if time has been synchronized
bool isTimeSynchronized();

// API handlers for time management
void handleGetTimeStatus(AsyncWebServerRequest *request);
void handleSetTimezone(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // TIME_MANAGER_H