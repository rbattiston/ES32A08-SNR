// WebServer.cpp
#include "WebServer.h"
#include "Utils.h"
#include "IOManager.h"
#include "WiFiManager.h"
#include "ModbusHandler.h"
#include "Scheduler.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

void initWebServer() {
  debugPrintln("DEBUG: Initializing web server...");
  
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving index.html");
    request->send(SPIFFS, "/index.html", String(), false);
  });
  
  // Routes for static files
  server.on("/css/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving style.css");
    request->send(SPIFFS, "/css/style.css", "text/css");
  });
  
  server.on("/js/dashboard.js", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving dashboard.js");
    request->send(SPIFFS, "/js/dashboard.js", "text/javascript");
  });
  
  // Initialize IO routes
  initIORoutes();
  
  // Initialize MODBUS routes
  initModbusRoutes();
  
  // Initialize Scheduler routes  // ADDED THIS LINE
  initSchedulerRoutes();          // ADDED THIS LINE
  
  // Instead of calling the missing functions, implement their functionality directly
  // or comment them out if not needed yet:
  
  // WiFi routes
  // WiFi API routes
  server.on("/api/wifi/status", HTTP_GET, handleGetWiFiStatus);

  server.on("/api/wifi/config", HTTP_POST, 
    [](AsyncWebServerRequest *request) {},
    NULL,
    handleSetWiFiCredentials
  );
  
  server.on("/api/wifi/test", HTTP_POST, 
    [](AsyncWebServerRequest *request) {},
    NULL,
    handleTestWiFiConnection
  );
  
  server.on("/wifi.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    debugPrintln("DEBUG: Serving wifi.html");
    request->send(SPIFFS, "/wifi.html", String(), false);
  });
  
  server.on("/js/wifi.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    debugPrintln("DEBUG: Serving wifi.js");
    request->send(SPIFFS, "/js/wifi.js", "text/javascript");
  });
  
  // Scheduler routes
  server.on("/scheduler.html", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving scheduler.html");
    request->send(SPIFFS, "/scheduler.html", String(), false);
  });
  
  server.on("/css/scheduler.css", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving scheduler.css");
    request->send(SPIFFS, "/css/scheduler.css", "text/css");
  });
  
  server.on("/js/scheduler.js", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving scheduler.js");
    request->send(SPIFFS, "/js/scheduler.js", "text/javascript");
  });
  
  // Add a not found handler
  server.onNotFound([](AsyncWebServerRequest *request) {
    String message = "DEBUG: Not found: " + request->url();
    debugPrintln(message.c_str());
    request->send(404, "text/plain", "Not found");
  });
  
  // Start server
  debugPrintln("DEBUG: Starting web server...");
  server.begin();
  debugPrintln("DEBUG: Web server started");
}

// Implement the IO routes here directly
void initIORoutes() {
  // Route for IO status
  server.on("/api/io/status", HTTP_GET, handleGetIOStatus);
  
  // Route for setting relay state
  server.on("/api/io/relay", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleSetRelay
  );
  
  // Route for setting all relay states
  server.on("/api/io/relays", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleSetAllRelays
  );
}

// Implement MODBUS routes
void initModbusRoutes() {
  // Route for MODBUS RTU tester page
  server.on("/modbus.html", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving modbus.html");
    request->send(SPIFFS, "/modbus.html", String(), false);
  });
  
  server.on("/js/modbus.js", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving modbus.js");
    request->send(SPIFFS, "/js/modbus.js", "text/javascript");
  });
  
  // Route for MODBUS API
  server.on("/api/modbus/request", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleModbusRequest
  );
}

// Implement Scheduler routes  // ADDED THIS ENTIRE FUNCTION
void initSchedulerRoutes() {
  debugPrintln("DEBUG: Initializing scheduler routes...");
  
  // Scheduler API endpoints
  server.on("/api/scheduler/save", HTTP_POST,
    // Empty onRequest lambda (required for onBody to work)
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument doc(4096);
      DeserializationError error = deserializeJson(doc, data, len);
      if (error) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
        return;
      }
      
      // Update light schedule if provided
      if (doc.containsKey("lightSchedule")) {
        JsonObject ls = doc["lightSchedule"];
        schedulerState.lightSchedule.lightsOnTime = ls["lightsOnTime"].as<String>();
        schedulerState.lightSchedule.lightsOffTime = ls["lightsOffTime"].as<String>();
      }
      
      // Update events if provided
      if (doc.containsKey("events")) {
        JsonArray events = doc["events"].as<JsonArray>();
        schedulerState.eventCount = 0;
        for (JsonObject evt : events) {
          if (schedulerState.eventCount < MAX_EVENTS) {
            Event &e = schedulerState.events[schedulerState.eventCount];
            e.id = evt["id"].as<String>();
            e.time = evt["time"].as<String>();
            int hour = 0, minute = 0;
            sscanf(e.time.c_str(), "%d:%d", &hour, &minute);
            e.startMinute = hour * 60 + minute;
            e.duration = evt["duration"].as<uint16_t>();
            e.relay = evt["relay"].as<uint8_t>();
            e.repeatCount = evt["repeatCount"].as<uint8_t>();  // Use repeatCount here
            e.repeatInterval = evt["repeatInterval"].as<uint16_t>();
            e.executedMask = 0;
            schedulerState.eventCount++;
          }
        }
      }
      
      // Now save the updated state to SPIFFS
      saveSchedulerState();
      debugPrintln("DEBUG: /api/scheduler/save called, state saved to SPIFFS");
      request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Scheduler state saved to SPIFFS\"}");
    }
  );
  
  server.on("/api/scheduler/load", HTTP_GET, handleLoadSchedulerState);
  
  server.on("/api/scheduler/status", HTTP_GET, handleSchedulerStatus);
  
  server.on("/api/scheduler/activate", HTTP_POST, handleActivateScheduler);
  
  server.on("/api/scheduler/deactivate", HTTP_POST, handleDeactivateScheduler);
  
  server.on("/api/relay/manual", HTTP_POST, 
    [](AsyncWebServerRequest *request) {},
    NULL,
    handleManualWatering
  );
  
  debugPrintln("DEBUG: Scheduler routes initialized");
}

void handleGetIOStatus(AsyncWebServerRequest *request) {
  debugPrintln("DEBUG: API request received: /api/io/status");
  
  // Debug print analog values before creating response
  debugPrintln("DEBUG: Analog values being sent:");
  float* voltageValues = getVoltageValues();
  float* currentValues = getCurrentValues();
  
  for (int i = 0; i < 4; i++) {
    debugPrintf("DEBUG: V%d=%.2fV, I%d=%.2fmA\n", 
              i+1, voltageValues[i], i+1, currentValues[i]);
  }
  
  DynamicJsonDocument doc(2048); // Increased size to ensure enough space
  
  // Add relay states
  JsonArray relays = doc.createNestedArray("relays");
  for (int i = 0; i < 8; i++) {
    JsonObject relay = relays.createNestedObject();
    relay["id"] = i;
    relay["state"] = (getRelayState() & (1 << i)) != 0;
  }
  
  // Add button states
  JsonArray buttons = doc.createNestedArray("buttons");
  bool* buttonStates = getButtonStates();
  for (int i = 0; i < 4; i++) {
    JsonObject button = buttons.createNestedObject();
    button["id"] = i;
    button["state"] = buttonStates[i];
  }
  
  // Add input states
  JsonArray inputs = doc.createNestedArray("inputs");
  bool* inputStates = getInputStates();
  for (int i = 0; i < 8; i++) {
    JsonObject input = inputs.createNestedObject();
    input["id"] = i;
    input["state"] = inputStates[i];
  }
  
  // Add voltage inputs
  JsonArray voltageInputsArray = doc.createNestedArray("voltageInputs");
  for (int i = 0; i < 4; i++) {
    JsonObject input = voltageInputsArray.createNestedObject();
    input["id"] = i;
    input["value"] = voltageValues[i];
  }
  
  // Add current inputs
  JsonArray currentInputsArray = doc.createNestedArray("currentInputs");
  for (int i = 0; i < 4; i++) {
    JsonObject input = currentInputsArray.createNestedObject();
    input["id"] = i;
    input["value"] = currentValues[i];
  }
  
  // Debug print the final JSON size
  debugPrintf("DEBUG: JSON document size: %d bytes\n", doc.memoryUsage());
  
  String response;
  serializeJson(doc, response);
  
  // Debug print a sample of the response
  debugPrint("DEBUG: JSON response sample: ");
  if (response.length() > 100) {
    String sample = response.substring(0, 100) + "...";
    debugPrintln(sample.c_str());
  } else {
    debugPrintln(response.c_str());
  }
  
  request->send(200, "application/json", response);
  debugPrintln("DEBUG: IO status sent to client");
}

void handleSetRelay(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("DEBUG: API request received: /api/io/relay");
  
  // Check if we received any data
  if (len == 0) {
    debugPrintln("DEBUG: No data received");
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No data received\"}");
    return;
  }
  
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  if (doc.containsKey("relay") && doc.containsKey("state")) {
    int relay = doc["relay"].as<int>();
    bool state = doc["state"].as<bool>();
    
    debugPrintf("DEBUG: Setting relay %d to %s\n", relay, state ? "ON" : "OFF");
    
    if (relay >= 0 && relay < 8) {
      setRelay(relay, state);
      
      // Create a more helpful response
      String responseJson = "{\"status\":\"success\",\"relay\":" + String(relay) + 
                           ",\"state\":" + (state ? "true" : "false") + 
                           ",\"relayState\":\"0x" + String(getRelayState(), HEX) + "\"}";
      
      request->send(200, "application/json", responseJson);
      return;
    } else {
      debugPrintln("DEBUG: Invalid relay ID");
    }
  } else {
    debugPrintln("DEBUG: Missing relay ID or state");
  }
  
  request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid relay ID or state\"}");
}

void handleSetAllRelays(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("DEBUG: API request received: /api/io/relays");
  
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  if (doc.containsKey("states")) {
    JsonArray states = doc["states"].as<JsonArray>();
    if (states.size() == 8) {
      debugPrintln("DEBUG: Setting all relays");
      uint8_t relayState = 0;
      for (int i = 0; i < 8; i++) {
        if (states[i].as<bool>()) {
          relayState |= (1 << i);
        }
      }
      
      setAllRelays(relayState);
      debugPrintf("DEBUG: New relay state: 0x%02X\n", getRelayState());
      request->send(200, "application/json", "{\"status\":\"success\"}");
      return;
    } else {
      debugPrintf("DEBUG: Expected 8 states, got %d\n", states.size());
    }
  } else {
    debugPrintln("DEBUG: Missing states array");
  }
  
  request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid relay states\"}");
}