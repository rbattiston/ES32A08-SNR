// WiFiManager.h
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h> // Add this include!

// Initialize WiFi manager
void initWiFiManager();

// Get AP credentials
const char* getAPSSID();
const char* getAPPassword();

// WiFi configuration structure
struct WiFiConfig {
  char ssid[32];
  char password[64];
  bool enabled;
};

// WiFi station functions
void setupDualWiFi();
void saveWiFiConfig();
void loadWiFiConfig();
void WiFiEventHandler(WiFiEvent_t event);

// WiFi test functions
void* createWiFiTestParam(const char* ssid, const char* password);

// External variables
extern WiFiConfig wifiStationConfig;
extern volatile bool wifiTestInProgress;
extern bool stationConnected;


// Add these function declarations
void handleGetWiFiStatus(AsyncWebServerRequest *request);
void handleSetWiFiCredentials(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleTestWiFiConnection(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // WIFI_MANAGER_H