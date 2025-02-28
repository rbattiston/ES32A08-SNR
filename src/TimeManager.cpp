#include "TimeManager.h"
#include <Arduino.h>
#include <WiFi.h>   // Required for WiFi.status()
#include "Utils.h"  // For debugPrintln and debugPrintf
#include <time.h>

// NTP configuration parameters
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";

// Initializes the time using NTP servers and sets the timezone to Toronto.
void initTimeManager() {
  debugPrintln("DEBUG: Initializing time via NTP with Toronto timezone");
  
  // Wait until WiFi Station Mode is enabled (up to 30 seconds).
  int waitCount = 0;
  while (WiFi.status() != WL_CONNECTED && waitCount < 30) {
    debugPrintln("DEBUG: Waiting for WiFi Station Mode to be enabled...");
    delay(1000);
    waitCount++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    debugPrintln("DEBUG: WiFi Station Mode not enabled after timeout; proceeding without NTP sync");
    // Optionally, you could return here instead of proceeding.
  }
  
  // Set the timezone to Toronto using the POSIX TZ string.
  setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
  tzset();
  debugPrintln("DEBUG: Timezone set to EST5EDT (Toronto)");
  
  // Configure time with 0 offsets; the TZ variable handles local conversion.
  configTime(0, 0, ntpServer1, ntpServer2);
  
  // Wait for NTP time to be set (up to 10 seconds).
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) {
    debugPrintln("DEBUG: Waiting for NTP time to be set...");
    delay(1000);
    retry++;
  }
  
  if (retry < 10) {
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%c", &timeinfo);
    debugPrintf("DEBUG: Time synchronized: %s\n", timeStr);
  } else {
    debugPrintln("DEBUG: Failed to obtain NTP time.");
  }
}

// Background task that checks every 60 seconds if local time is available.
// If getLocalTime() fails (for example, due to lost WiFi connection), it reconfigures NTP.
void ntpManagerTask(void *parameter) {
  for (;;) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      debugPrintln("DEBUG: getLocalTime() failed, attempting NTP re-sync");
      if (WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, ntpServer1, ntpServer2);
        delay(5000);
      } else {
        debugPrintln("DEBUG: WiFi not connected, cannot re-sync NTP");
      }
    } else {
      char timeStr[64];
      strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
      debugPrintf("DEBUG: Current time: %s\n", timeStr);
    }
    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

// Starts the background task to monitor and re-sync NTP.
void startTimeManagerTask() {
  xTaskCreatePinnedToCore(ntpManagerTask, "NTPManagerTask", 4096, NULL, 1, NULL, 1);
}
