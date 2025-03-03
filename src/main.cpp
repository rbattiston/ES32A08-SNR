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
#include "TimeManager.h"
#include "MemoryManager.h" // Add the new memory management header

// Add these variables for WiFi monitoring
TaskHandle_t wifiMonitorTaskHandle = NULL;
const uint32_t WIFI_MONITOR_DELAY_MS = 15000; // 15 seconds

// WiFi monitoring task
void wifiMonitorTask(void *pvParameters) {
  uint32_t lastCheckTime = 0;
  uint32_t lastDisconnectTime = 0;
  bool wasConnected = false;
  int disconnectCount = 0;
  
  debugPrintln("DEBUG: WiFi monitor task started");
  
  for(;;) {
    uint32_t now = millis();
    bool isConnected = (WiFi.status() == WL_CONNECTED);
    
    // Check if state changed
    if (isConnected != wasConnected) {
      if (isConnected) {
        // Just connected
        disconnectCount = 0;
        debugPrintf("DEBUG: WiFi reconnected! IP: %s, RSSI: %d dBm\n", 
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
      } else {
        // Just disconnected
        lastDisconnectTime = now;
        disconnectCount++;
        debugPrintf("DEBUG: WiFi disconnected (count: %d)\n", disconnectCount);
      }
      wasConnected = isConnected;
    }
    
    // Perform periodic checks and diagnostics
    if (now - lastCheckTime >= WIFI_MONITOR_DELAY_MS) {
      lastCheckTime = now;
      
      // Check memory status
      debugPrintf("DEBUG: Memory stats - Free: %d bytes, Min free: %d bytes\n",
                 ESP.getFreeHeap(), ESP.getMinFreeHeap());
      
      // Check WiFi status
      if (isConnected) {
        debugPrintf("DEBUG: WiFi status - Connected, IP: %s, RSSI: %d dBm\n",
                   WiFi.localIP().toString().c_str(), WiFi.RSSI());
      } else {
        debugPrintf("DEBUG: WiFi status - Disconnected, mode: %d\n", WiFi.getMode());
        
        // If disconnected for too long, try to fix
        if (disconnectCount > 5 && (now - lastDisconnectTime) > 60000) { // 1 minute
          debugPrintln("DEBUG: Attempting WiFi recovery...");
          
          // Force WiFi restart
          WiFi.disconnect(true);
          delay(1000);
          WiFi.mode(WIFI_OFF);
          delay(1000);
          WiFi.mode(WIFI_AP_STA);
          delay(1000);
          
          // Reconnect
          if (strlen(wifiStationConfig.ssid) > 0 && wifiStationConfig.enabled) {
            WiFi.begin(wifiStationConfig.ssid, wifiStationConfig.password);
            debugPrintf("DEBUG: Reconnecting to %s...\n", wifiStationConfig.ssid);
          }
          
          disconnectCount = 0;
        }
      }
    }
    
    // Yield to other tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

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

// Declaration for the scheduler websocket functions
void initSchedulerWebSocket();
void updateSchedulerWebSocket();

// Serial command processing
void processDebugCommand() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "help") {
      Serial.println("Available commands:");
      Serial.println("  time - Check time synchronization");
      Serial.println("  relay - Test relay control");
      Serial.println("  start - Start the scheduler");
      Serial.println("  stop - Stop the scheduler");
      Serial.println("  trigger <schedule> <eventId> - Trigger specific event");
      Serial.println("  help - Show this help");
    }
    else if (command == "time") {
      verifyTimeSync();
    }
    else if (command == "relay") {
      testRelayControl();
    }
    else if (command == "start") {
      Serial.println("Starting scheduler...");
      startSchedulerTask();
    }
    else if (command == "stop") {
      Serial.println("Stopping scheduler...");
      stopSchedulerTask();
    }
    else if (command.startsWith("trigger ")) {
      // Extract schedule and event ID
      int firstSpace = command.indexOf(' ');
      int secondSpace = command.indexOf(' ', firstSpace + 1);
      
      if (secondSpace > 0) {
        String scheduleName = command.substring(firstSpace + 1, secondSpace);
        String eventId = command.substring(secondSpace + 1);
        
        Serial.printf("Manually triggering event '%s' in schedule '%s'\n", 
                    eventId.c_str(), scheduleName.c_str());
        manuallyTriggerEvent(scheduleName.c_str(), eventId.c_str());
      }
      else {
        Serial.println("Invalid trigger command. Format: trigger <schedule> <eventId>");
      }
    }
    else {
      Serial.println("Unknown command. Type 'help' for available commands.");
    }
  }
}

void timeMonitorTask(void *pvParameters) {
  for (;;) {
    // Get current time
    time_t now = time(NULL);
    struct tm utc_time, local_time;
    gmtime_r(&now, &utc_time);
    localtime_r(&now, &local_time);
    
    // Format times
    char utcStr[20], localStr[20];
    strftime(utcStr, sizeof(utcStr), "%H:%M:%S", &utc_time);
    strftime(localStr, sizeof(localStr), "%H:%M:%S", &local_time);
    
    debugPrintln("\n----- TIME DEBUG -----");
    debugPrintf("UTC time: %s\n", utcStr);
    debugPrintf("Local time: %s\n", localStr);
    debugPrintf("Time difference: %d hours\n", local_time.tm_hour - utc_time.tm_hour);
    debugPrintln("---------------------\n");
    
    // Wait 30 seconds before checking again
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

// Initialize components
void setup() {
  // Initialize serial for debug output
  Serial.begin(115200);
  delay(500);
  
  debugPrintln("\n\n-------------------------");
  debugPrintln("ES32A08 Setup Utility Starting...");
  debugPrintln("-------------------------");
  
  // Increase watchdog timeout to prevent spurious resets
  disableLoopWDT();
  disableCore0WDT();
  disableCore1WDT();
  esp_task_wdt_init(30, false); // 30 second timeout, don't panic on timeout
  
  // Set CPU frequencies to optimize stability
  setCpuFrequencyMhz(160); // Lower frequency for better stability (default is 240MHz)
  
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
  
  // Initialize Memory Management
  initMemoryManager();
  
  // Reduce memory usage after setup
  performMemoryOptimization();
  
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
    1 // Run on core 1
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
  
  // Start WiFi monitoring task
  xTaskCreatePinnedToCore(
    wifiMonitorTask,
    "WiFiMonitor",
    4096,
    NULL,
    1,
    &wifiMonitorTaskHandle,
    1 // Run on core 1
  );
  
  // Start scheduler monitoring task
  xTaskCreatePinnedToCore(
    schedulerMonitorTask,
    "SchMonitor",
    4096,
    NULL,
    1,
    NULL,
    1  // Run on core 1
  );
  // Initialize scheduler WebSockets
  initSchedulerWebSocket();
  
// Start automated scheduler diagnostics
xTaskCreatePinnedToCore(
  schedulerDiagnosticsTask,
  "SchDiagnostics",
  4096,
  NULL,
  1,
  NULL,
  1
);

// Start immediate execution task for testing
xTaskCreatePinnedToCore(
  immediateExecutionTask,
  "ImmediateExec",
  4096,
  NULL,
  1,
  NULL,
  1
);

// Start immediate execution task for testing
xTaskCreatePinnedToCore(
  timeMonitorTask,
  "TimeMonitor",
  4096,
  NULL,
  1,
  NULL,
  1
);

  debugPrintln("-------------------------");
  debugPrintf("Connect to WiFi SSID: %s with password: %s\n", getAPSSID(), getAPPassword());
  debugPrintf("Then navigate to http://%s in your browser\n", WiFi.softAPIP().toString().c_str());
  debugPrintln("-------------------------");
}

void loop() {
  // Main loop only handles heartbeat and housekeeping
  static uint32_t heartbeatTime = 0;
  static uint32_t lastMemoryOptimizationTime = 0;
  static uint32_t lastIntegrityCheckTime = 0;
  
  uint32_t currentTime = millis();
  
  // Heartbeat and logging every 10 seconds
  if (currentTime - heartbeatTime > 10000) {
    heartbeatTime = currentTime;
    
    switchToDebugMode();
    debugPrintln("DEBUG: Heartbeat - ESP32 still running");
    
    // Reduced logging to minimize UART activity which can affect WiFi
    debugPrintf("DEBUG: Free heap: %d bytes\n", ESP.getFreeHeap());
    
    // Only log WiFi stats if there's a change
    static bool lastWiFiStatus = false;
    bool currentWiFiStatus = (WiFi.status() == WL_CONNECTED);
    
    if (currentWiFiStatus != lastWiFiStatus) {
      debugPrintf("DEBUG: WiFi status changed to: %s\n", 
                 currentWiFiStatus ? "CONNECTED" : "DISCONNECTED");
      lastWiFiStatus = currentWiFiStatus;
    }
  }
  
  // Perform memory optimization periodically (every minute)
  if (currentTime - lastMemoryOptimizationTime > 60000) {
    lastMemoryOptimizationTime = currentTime;
    performMemoryOptimization();
  }
  
  // Perform heap integrity check periodically (every 5 minutes)
  if (currentTime - lastIntegrityCheckTime > 300000) {
    lastIntegrityCheckTime = currentTime;
    checkHeapIntegrity();
  }
  
  // Update scheduler WebSockets (handles timeouts)
  updateSchedulerWebSocket();

  processDebugCommand();

  // Efficient delay that allows other tasks to run
  vTaskDelay(pdMS_TO_TICKS(1000));
}