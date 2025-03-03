#include "TimeManager.h"
#include <Arduino.h>
#include <WiFi.h>
#include "Utils.h"
#include <time.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// NTP configuration parameters
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";

// Default timezone (Toronto)
const char* defaultTimezone = "EST5EDT,M3.2.0/2,M11.1.0/2";

// Global variables
static char currentTimezone[64] = "EST5EDT,M3.2.0/2,M11.1.0/2"; // Default to Toronto timezone
static time_t firstSyncTime = 0;
static bool timeSynchronized = false;
static bool timezoneLoaded = false;

// Save timezone to SPIFFS
void saveTimezone() {
  File file = SPIFFS.open("/timezone.json", FILE_WRITE);
  if (!file) {
    debugPrintln("DEBUG: Failed to open timezone file for writing");
    return;
  }
  
  DynamicJsonDocument doc(128);
  doc["timezone"] = currentTimezone;
  
  if (serializeJson(doc, file) == 0) {
    debugPrintln("DEBUG: Failed to write timezone to file");
  }
  
  file.close();
}

// Load timezone from SPIFFS
void loadTimezone() {
  if (timezoneLoaded) return; // Only load once
  
  if (!SPIFFS.exists("/timezone.json")) {
    debugPrintln("DEBUG: Timezone file not found, using default (Toronto)");
    strlcpy(currentTimezone, defaultTimezone, sizeof(currentTimezone));
    saveTimezone();
    timezoneLoaded = true;
    return;
  }
  
  File file = SPIFFS.open("/timezone.json", FILE_READ);
  if (!file) {
    debugPrintln("DEBUG: Failed to open timezone file for reading");
    timezoneLoaded = true;
    return;
  }
  
  DynamicJsonDocument doc(128);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    debugPrintf("DEBUG: Failed to parse timezone JSON: %s\n", error.c_str());
    timezoneLoaded = true;
    return;
  }

  strlcpy(currentTimezone, doc["timezone"] | defaultTimezone, sizeof(currentTimezone));
  debugPrintf("DEBUG: Loaded timezone: %s\n", currentTimezone);
  timezoneLoaded = true;
}

// Initializes time settings but doesn't wait for WiFi
void initTimeManager() {
  debugPrintln("DEBUG: Initializing time manager");
  
  // Load timezone from SPIFFS
  loadTimezone();
  
  // Set the timezone using the loaded value
  setenv("TZ", currentTimezone, 1);
  tzset();
  debugPrintf("DEBUG: Timezone set to: %s\n", currentTimezone);
  
  // We won't try to sync with NTP here
  // NTP sync will happen in the WiFi event handler when station mode connects
  debugPrintln("DEBUG: NTP sync will be performed when WiFi station connects");
}

// Set timezone and apply it
bool setTimezone(const char* tz) {
  if (!tz || strlen(tz) == 0 || strlen(tz) >= sizeof(currentTimezone)) {
    return false;
  }
  
  // Save the new timezone
  strlcpy(currentTimezone, tz, sizeof(currentTimezone));
  
  // Apply the timezone
  setenv("TZ", currentTimezone, 1);
  tzset();
  
  // Save to SPIFFS
  saveTimezone();
  
  debugPrintf("DEBUG: Timezone changed to: %s\n", currentTimezone);
  return true;
}

// Get current timezone
const char* getCurrentTimezone() {
  return currentTimezone;
}

// Get first sync time
time_t getFirstSyncTime() {
  return firstSyncTime;
}

// Check if time is synchronized
bool isTimeSynchronized() {
  return timeSynchronized;
}

// Background task that monitors time and syncs with NTP when needed
void ntpManagerTask(void *parameter) {
  uint32_t lastSyncAttempt = 0;
  
  for (;;) {
    struct tm timeinfo;
    bool timeAvailable = getLocalTime(&timeinfo);
    
    if (WiFi.status() == WL_CONNECTED) {
      // WiFi is connected
      
      // If time is not available or it's been more than 24 hours since last sync attempt
      if (!timeAvailable || (millis() - lastSyncAttempt > 24*60*60*1000UL)) {
        debugPrintln("DEBUG: WiFi connected, attempting NTP sync");
        
        // Configure NTP servers
        configTime(0, 0, ntpServer1, ntpServer2);
        lastSyncAttempt = millis();
        
        // Wait for up to 5 seconds for time to be set
        for (int i = 0; i < 5; i++) {
          delay(1000);
          if (getLocalTime(&timeinfo)) {
            // Time sync successful
            time_t now;
            time(&now);
            
            if (!timeSynchronized) {
              // First successful sync
              firstSyncTime = now;
              timeSynchronized = true;
              debugPrintln("DEBUG: First time synchronization successful");
            }
            
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%c", &timeinfo);
            debugPrintf("DEBUG: Time synchronized: %s\n", timeStr);
            break;
          }
        }
      } else if (timeAvailable) {
        // Time is already set and WiFi is connected
        
        // If this is the first successful sync (maybe WiFi connected after boot)
        if (!timeSynchronized) {
          time_t now;
          time(&now);
          firstSyncTime = now;
          timeSynchronized = true;
          debugPrintln("DEBUG: First time synchronization detected");
        }
        
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
        debugPrintf("DEBUG: Current time: %s\n", timeStr);
      }
    } else {
      // WiFi not connected
      if (timeAvailable) {
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
        debugPrintf("DEBUG: Current time: %s (WiFi disconnected)\n", timeStr);
      } else {
        debugPrintln("DEBUG: Time not set and WiFi not connected");
      }
    }
    
    // Check every minute
    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

// Starts the background task to monitor and re-sync NTP.
void startTimeManagerTask() {
  xTaskCreatePinnedToCore(ntpManagerTask, "NTPManagerTask", 4096, NULL, 1, NULL, 1);
}

// API handler for getting time status
void handleGetTimeStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(512);
  
  // Add time zone info
  time_t now = time(NULL);
  struct tm localTime, utcTime;
  localtime_r(&now, &localTime);
  gmtime_r(&now, &utcTime);

  // Calculate time zone offset in hours
  int offsetHours = localTime.tm_hour - utcTime.tm_hour;
  // Handle day boundary crossings
  if (offsetHours > 12) offsetHours -= 24;
  if (offsetHours < -12) offsetHours += 24;

  // Determine DST status
  bool isDST = localTime.tm_isdst > 0;

  doc["timezoneOffset"] = offsetHours;
  doc["isDST"] = isDST;

  // Get current time
  struct tm timeinfo;
  time(&now);
  bool hasTime = getLocalTime(&timeinfo);
  
  doc["synchronized"] = timeSynchronized;
  doc["timezone"] = currentTimezone;
  
  if (hasTime) {
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    doc["currentTime"] = timeStr;
  } else {
    doc["currentTime"] = "Unknown";
  }
  
  if (firstSyncTime > 0) {
    struct tm firstSyncTm;
    localtime_r(&firstSyncTime, &firstSyncTm);
    char firstSyncStr[32];
    strftime(firstSyncStr, sizeof(firstSyncStr), "%Y-%m-%d %H:%M:%S", &firstSyncTm);
    doc["firstSyncTime"] = firstSyncStr;
  } else {
    doc["firstSyncTime"] = "Never";
  }
  
  String response;
  serializeJson(doc, response);
  
  request->send(200, "application/json", response);
}

// API handler for setting timezone
void handleSetTimezone(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  if (!doc.containsKey("timezone")) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing timezone parameter\"}");
    return;
  }
  
  const char* timezone = doc["timezone"].as<const char*>();
  
  if (setTimezone(timezone)) {
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Timezone updated successfully\"}");
  } else {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid timezone format\"}");
  }
}