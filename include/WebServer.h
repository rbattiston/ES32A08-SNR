// WebServer.h
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Ensure the server is available for all files
extern AsyncWebServer server;  // Make sure this is declared here

// Initialize web server
void initWebServer();

// Route handlers
void initWiFiRoutes();
void initIORoutes();
void initModbusRoutes();
void initSchedulerRoutes();
void initTimeRoutes();  // New function for time routes

// API handlers for IO
void handleGetIOStatus(AsyncWebServerRequest *request);
void handleSetRelay(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleSetAllRelays(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// API handlers for WiFi
void handleGetWiFiStatus(AsyncWebServerRequest *request);
void handleSetWiFiCredentials(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleTestWiFiConnection(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// API handlers for MODBUS
void handleModbusRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// External variables
extern AsyncWebServer server;

#endif // WEB_SERVER_H