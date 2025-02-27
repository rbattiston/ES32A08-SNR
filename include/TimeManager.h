#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <time.h>

// Initializes the time manager (sets up NTP and starts the background task)
void initTimeManager();

// Starts a background task that monitors and re-syncs NTP if needed.
void startTimeManagerTask();

#endif // TIME_MANAGER_H
