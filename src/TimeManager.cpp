#include "TimeManager.h"
#include <Arduino.h>
#include <WiFi.h>
#include "Utils.h"  // For debugPrintln and debugPrintf
#include <time.h>

// Set the timezone for Toronto: EST5EDT,M3.2.0/2,M11.1.0/2
// This means standard time is UTC-5 and DST is applied as appropriate.
void initTimeManager() {
  debugPrintln("DEBUG: Initializing time via NTP with Toronto timezone");

  // Set the TZ environment variable and update time zone settings.
  setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
  tzset();
  debugPrintln("DEBUG: Timezone set to EST5EDT (Toronto)");

  // Configure time with 0 offsets since the TZ environment variable handles it.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // Wait for NTP time to be set.
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

// Background task to check and re-sync NTP every 60 seconds.
void ntpManagerTask(void *parameter) {
  for (;;) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      debugPrintln("DEBUG: getLocalTime() failed, attempting NTP re-sync");
      if (WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
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

void startTimeManagerTask() {
  xTaskCreatePinnedToCore(ntpManagerTask, "NTPManagerTask", 4096, NULL, 1, NULL, 1);
}
