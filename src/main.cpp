#include <Arduino.h>
#include "WebServer.h"
#include "WiFiManager.h"
#include "IOManager.h"
#include "Scheduler.h"
#include "ModbusHandler.h"
#include "Utils.h"
#include <SPIFFS.h>
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "TimeManager.h" // Make sure this includes the updated TimeManager.h

void testSPIFFSWrite() {
  File testFile = SPIFFS.open("/test.txt", FILE_WRITE);
  if (!testFile) {
    Serial.println("Failed to open test file for writing");
    return;
  }
  testFile.println("This is a test.");
  testFile.close();
  listSPIFFSFiles();
}

// Initialize components
void setup() {
  // Initialize serial for debug output
  Serial.begin(115200);
  delay(500);
  
  debugPrintln("\n\n-------------------------");
  debugPrintln("ES32A08 Setup Utility Starting...");
  debugPrintln("-------------------------");
  
  // Disable watchdogs to prevent resets
  disableLoopWDT();
  disableCore0WDT();
  disableCore1WDT();
  esp_task_wdt_init(15, true); // 15 second timeout

  // Initialize SPIFFS
  debugPrintln("DEBUG: Initializing SPIFFS...");
  if(!SPIFFS.begin(true)){
    debugPrintln("ERROR: SPIFFS initialization failed!");
    return;
  }
  debugPrintln("DEBUG: SPIFFS initialized successfully");
  
  // List available files
  listSPIFFSFiles();
  testSPIFFSWrite();
  listSPIFFSFiles();
  
  // Initialize WiFi first to get AP up quickly
  debugPrintln("DEBUG: Initializing WiFi Manager (AP mode first)...");
  initWiFiManager();
  
  // Then initialize other components
  debugPrintln("DEBUG: Initializing IO Manager...");
  initIOManager();
  
  debugPrintln("DEBUG: Initializing Time Manager...");
  initTimeManager();
  startTimeManagerTask(); // This will monitor for WiFi connection before syncing
  
  debugPrintln("DEBUG: Initializing Scheduler...");
  initScheduler();
  
  debugPrintln("DEBUG: Initializing Modbus Handler...");
  initModbusHandler();
  
  // Initialize and start web server
  debugPrintln("DEBUG: Initializing Web Server...");
  initWebServer();
  debugPrintln("DEBUG: Setup complete!");
  
  // Create monitoring task
  xTaskCreatePinnedToCore(
    monitorTask,
    "Monitor",
    2048,
    NULL,
    1,
    NULL,
    0 // Run on core 0
  );
  
  // Create watchdog task
  xTaskCreate(
    watchdogTask,
    "Watchdog",
    2048,
    NULL,
    1,
    NULL
  );
  
  debugPrintln("-------------------------");
  debugPrintf("Connect to WiFi SSID: %s with password: %s\n", getAPSSID(), getAPPassword());
  debugPrintf("Then navigate to http://%s in your browser\n", WiFi.softAPIP().toString().c_str());
  debugPrintln("-------------------------");
}

void loop() {
  // Main loop only handles heartbeat and housekeeping
  static uint32_t heartbeatTime = 0;
  if (millis() - heartbeatTime > 10000) {
    heartbeatTime = millis();
    
    switchToDebugMode();
    debugPrintln("DEBUG: Heartbeat - ESP32 still running");
    
    // Print memory stats
    debugPrintf("DEBUG: Free heap: %d bytes\n", ESP.getFreeHeap());
    debugPrintf("DEBUG: Minimum free heap: %d bytes\n", ESP.getMinFreeHeap());
    
    // Print WiFi stats
    debugPrintf("DEBUG: WiFi AP status: %d, Stations connected: %d\n", 
               WiFi.status(), WiFi.softAPgetStationNum());
  }
  
  vTaskDelay(pdMS_TO_TICKS(1000)); // Prevent watchdog issues
}