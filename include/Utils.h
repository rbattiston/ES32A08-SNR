// Utils.h
#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

// Debug output functions
void switchToDebugMode();
void switchToRS485Mode();
void debugPrint(const char* message);
void debugPrintln(const char* message);
void debugPrintf(const char* format, ...);

// SPIFFS utilities
void listSPIFFSFiles();

// System tasks
void monitorTask(void *pvParameters);
void watchdogTask(void *pvParameters);

// UART mode tracking
enum UartMode {
  UART_MODE_DEBUG,
  UART_MODE_RS485
};

extern UartMode currentUartMode;

#endif // UTILS_H