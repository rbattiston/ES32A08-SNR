// Utils.cpp
#include "Utils.h"
#include <SPIFFS.h>
#include "PinConfig.h"
#include "esp_task_wdt.h"

UartMode currentUartMode = UART_MODE_DEBUG;

void switchToDebugMode() {
  if (currentUartMode == UART_MODE_DEBUG) {
    return;  // Already in debug mode
  }
  
  // Flush any pending data
  Serial.flush();
  
  // Reconfigure for debug
  Serial.begin(115200);
  
  currentUartMode = UART_MODE_DEBUG;
  delay(10);  // Small delay for UART to stabilize
}

void switchToRS485Mode() {
  if (currentUartMode == UART_MODE_RS485) {
    return;  // Already in RS485 mode
  }
  
  // Flush any pending debug data
  Serial.flush();
  
  // Reconfigure for RS485
  Serial.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW);  // Default to receive mode
  
  currentUartMode = UART_MODE_RS485;
  delay(10);  // Small delay for UART to stabilize
}

void debugPrint(const char* message) {
  switchToDebugMode();
  Serial.print(message);
}

void debugPrintln(const char* message) {
  switchToDebugMode();
  Serial.println(message);
}

void debugPrintf(const char* format, ...) {
  switchToDebugMode();
  
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsprintf(buffer, format, args);
  va_end(args);
  
  Serial.print(buffer);
}

void listSPIFFSFiles() {
  debugPrintln("DEBUG: SPIFFS files:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while(file){
    debugPrint("  - ");
    debugPrint(file.name());
    debugPrint(" (");
    char sizeBuf[16];
    sprintf(sizeBuf, "%d", file.size());
    debugPrint(sizeBuf);
    debugPrintln(" bytes)");
    file = root.openNextFile();
  }
}

void monitorTask(void *pvParameters) {
  for(;;) {
    // Log memory usage
    debugPrintf("DEBUG: Free heap: %d bytes, Min free heap: %d bytes\n", 
               ESP.getFreeHeap(), ESP.getMinFreeHeap());
    
    // Feed the watchdog
    esp_task_wdt_reset();
    
    vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
  }
}

void watchdogTask(void *pvParameters) {
  for(;;) {
    // Feed the watchdog
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}