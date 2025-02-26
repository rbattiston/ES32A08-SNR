#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "PinConfig.h"
#include "TestMode.h"

// WiFi settings
const char* ssid = "ES32A08-Setup"; // Default AP SSID
const char* password = "password";  // Default AP password

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Global variables (matching the existing TestMode.cpp)
extern volatile uint8_t relayState;  // Declared in TestMode.cpp
extern uint8_t Read_74HC165();       // Function from TestMode.cpp
extern uint8_t Get_DI_Value();       // Function from TestMode.cpp

// Add global variables for analog values
float voltageValues[4] = {0.0, 0.0, 0.0, 0.0};
float currentValues[4] = {0.0, 0.0, 0.0, 0.0};
bool buttonStates[4] = {false, false, false, false};
bool inputStates[8] = {false, false, false, false, false, false, false, false};

// For MODBUS communications
#define MODBUS_BUFFER_SIZE 256
uint8_t modbusRequestBuffer[MODBUS_BUFFER_SIZE];
uint8_t modbusResponseBuffer[MODBUS_BUFFER_SIZE];
bool rs485Initialized = false;

// UART mode tracking
enum UartMode {
  UART_MODE_DEBUG,
  UART_MODE_RS485
};

UartMode currentUartMode = UART_MODE_DEBUG;

// Initialize flags for task completion
volatile bool initTestModeComplete = false;

// Switch UART between debug and RS485 modes
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

// Safe debug print that ensures we're in debug mode
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

// RS485 transmit control
void rs485Transmit(bool enable) {
  digitalWrite(RS485_DE, enable);
  delay(1); // Short delay for line settling
  
  // No debug print here to avoid switching UARTs during critical timing
}

// Task for initializing 74HC595 with timeout
void initTestModeTask(void *pvParameters) {
  debugPrintln("DEBUG: In initTestMode task...");
  
  // Initialize the shift register pins directly
  pinMode(SH595_DATA, OUTPUT);
  pinMode(SH595_CLOCK, OUTPUT);
  pinMode(SH595_LATCH, OUTPUT);
  pinMode(SH595_OE, OUTPUT);
  
  // Try to clear display - simplified version
  digitalWrite(SH595_LATCH, LOW);
  for (int i = 0; i < 24; i++) {
    digitalWrite(SH595_DATA, LOW);
    digitalWrite(SH595_CLOCK, HIGH);
    digitalWrite(SH595_CLOCK, LOW);
  }
  digitalWrite(SH595_LATCH, HIGH);
  digitalWrite(SH595_OE, LOW);  // Enable outputs
  
  // Try a more careful approach to the original function
  // but with timeouts to prevent hanging
  try {
    debugPrintln("DEBUG: Running original initTestMode...");
    unsigned long startTime = millis();
    initTestMode();
    debugPrintln("DEBUG: initTestMode completed successfully");
  } catch (...) {
    debugPrintln("DEBUG: Exception in initTestMode");
  }
  
  initTestModeComplete = true;
  vTaskDelete(NULL);
}

// CRC16 calculation for MODBUS
uint16_t calculateCRC16(uint8_t* buffer, uint8_t length) {
  uint16_t crc = 0xFFFF;
  
  for (uint8_t i = 0; i < length; i++) {
    crc ^= buffer[i];
    
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  
  return crc;
}

// Send MODBUS request and get response
bool sendModbusRequest(uint8_t* request, uint8_t requestLength, uint8_t* response, uint8_t& responseLength) {
  debugPrintln("DEBUG: Sending MODBUS request...");
  
  // Print request bytes for debugging
  debugPrint("DEBUG: Request bytes: ");
  for (int i = 0; i < requestLength; i++) {
    debugPrintf("%02X ", request[i]);
  }
  debugPrintln("");
  
  // Switch to RS485 mode
  switchToRS485Mode();
  
  // Clear any pending data
  while (Serial.available()) {
    Serial.read();
  }
  
  // Enable transmitter
  rs485Transmit(true);
  
  // Send request
  Serial.write(request, requestLength);
  Serial.flush();
  
  // Switch to receive mode
  rs485Transmit(false);
  
  // Wait for response with timeout
  uint32_t startTime = millis();
  uint32_t waitTime = 0;
  bool responseReceived = false;
  
  while (waitTime < 1000) {
    if (Serial.available() >= 5) {
      responseReceived = true;
      break;
    }
    delay(10);
    waitTime = millis() - startTime;
  }
  
  // Read response
  responseLength = 0;
  while (Serial.available() && responseLength < MODBUS_BUFFER_SIZE) {
    response[responseLength++] = Serial.read();
  }
  
  // Switch back to debug mode
  switchToDebugMode();
  
  debugPrintf("DEBUG: Received %d bytes\n", responseLength);
  
  // Print response bytes for debugging
  if (responseLength > 0) {
    debugPrint("DEBUG: Response bytes: ");
    for (int i = 0; i < responseLength; i++) {
      debugPrintf("%02X ", response[i]);
    }
    debugPrintln("");
  }
  
  // Check if we received a valid response
  if (!responseReceived || responseLength < 5) {
    debugPrintln("DEBUG: Response too short or timed out");
    return false; // Timeout or invalid response
  }
  
  // Validate CRC
  uint16_t receivedCRC = (response[responseLength - 1] << 8) | response[responseLength - 2];
  uint16_t calculatedCRC = calculateCRC16(response, responseLength - 2);
  
  debugPrintf("DEBUG: CRC check - Received: 0x%04X, Calculated: 0x%04X\n", receivedCRC, calculatedCRC);
  
  return (receivedCRC == calculatedCRC);
}

// Task to update analog values
void vAnalogTask(void *pvParameters) {
  debugPrintln("DEBUG: Analog task started");
  
  for (;;) {
    // Read analog inputs (similar to sensorTestLoop in TestMode.cpp)
    int analog_value[8];
    
    // Safely read analog inputs with error handling
    try {
      analog_value[0] = analogRead(AI_V1);
    } catch (...) {
      analog_value[0] = 0;
    }
    
    try {
      analog_value[1] = analogRead(AI_V2);
    } catch (...) {
      analog_value[1] = 0;
    }
    
    try {
      analog_value[2] = analogRead(AI_V3);
    } catch (...) {
      analog_value[2] = 0;
    }
    
    try {
      analog_value[3] = analogRead(AI_V4);
    } catch (...) {
      analog_value[3] = 0;
    }
    
    try {
      analog_value[4] = analogRead(AI_I1);
    } catch (...) {
      analog_value[4] = 0;
    }
    
    try {
      analog_value[5] = analogRead(AI_I2);
    } catch (...) {
      analog_value[5] = 0;
    }
    
    try {
      analog_value[6] = analogRead(AI_I3);
    } catch (...) {
      analog_value[6] = 0;
    }
    
    try {
      analog_value[7] = analogRead(AI_I4);
    } catch (...) {
      analog_value[7] = 0;
    }
    
    // Convert to voltage (same formula as in TestMode.cpp)
    voltageValues[0] = (float)analog_value[0] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
    voltageValues[1] = (float)analog_value[1] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
    voltageValues[2] = (float)analog_value[2] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
    voltageValues[3] = (float)analog_value[3] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
    
    // Convert to current (same formula as in TestMode.cpp)
    currentValues[0] = ((float)analog_value[4] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
    currentValues[1] = ((float)analog_value[5] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
    currentValues[2] = ((float)analog_value[6] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
    currentValues[3] = ((float)analog_value[7] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
    
    // Read button states - with error handling
    try {
      buttonStates[0] = (digitalRead(BTN1) == LOW);
      buttonStates[1] = (digitalRead(BTN2) == LOW);
      buttonStates[2] = (digitalRead(BTN3) == LOW);
      buttonStates[3] = (digitalRead(BTN4) == LOW);
    } catch (...) {
      // Keep previous button states on error
    }
    
    // Read input states from 74HC165 - only if initialization completed
    if (initTestModeComplete) {
      try {
        uint8_t diStatus = Get_DI_Value();
        for (int i = 0; i < 8; i++) {
          inputStates[i] = (diStatus & (1 << i)) != 0;
        }
      } catch (...) {
        // Keep previous input states on error
      }
    }
    
    // Print debug every 5 seconds
    static uint32_t lastDebugTime = 0;
    if (millis() - lastDebugTime > 5000) {
      lastDebugTime = millis();
      debugPrintln("DEBUG: Analog readings update...");
      debugPrintf("V1=%.2fV, V2=%.2fV, V3=%.2fV, V4=%.2fV\n", 
                  voltageValues[0], voltageValues[1], voltageValues[2], voltageValues[3]);
      debugPrintf("I1=%.2fmA, I2=%.2fmA, I3=%.2fmA, I4=%.2fmA\n", 
                  currentValues[0], currentValues[1], currentValues[2], currentValues[3]);
      debugPrintf("BTN: %d %d %d %d, Relay state: 0x%02X\n", 
                  buttonStates[0], buttonStates[1], buttonStates[2], buttonStates[3], relayState);
    }
    
    vTaskDelay(pdMS_TO_TICKS(200)); // Update every 200ms
  }
}

// Handler for IO status
void handleGetIOStatus(AsyncWebServerRequest *request) {
  debugPrintln("DEBUG: API request received: /api/io/status");
  
  DynamicJsonDocument doc(1024);
  
  // Add relay states
  JsonArray relays = doc.createNestedArray("relays");
  for (int i = 0; i < 8; i++) {
    JsonObject relay = relays.createNestedObject();
    relay["id"] = i;
    relay["state"] = (relayState & (1 << i)) != 0;
  }
  
  // Add button states
  JsonArray buttons = doc.createNestedArray("buttons");
  for (int i = 0; i < 4; i++) {
    JsonObject button = buttons.createNestedObject();
    button["id"] = i;
    button["state"] = buttonStates[i];
  }
  
  // Add input states
  JsonArray inputs = doc.createNestedArray("inputs");
  for (int i = 0; i < 8; i++) {
    JsonObject input = inputs.createNestedObject();
    input["id"] = i;
    input["state"] = inputStates[i];
  }
  
  // Add analog inputs
  JsonArray voltageInputs = doc.createNestedArray("voltageInputs");
  for (int i = 0; i < 4; i++) {
    JsonObject input = voltageInputs.createNestedObject();
    input["id"] = i;
    input["value"] = voltageValues[i];
  }
  
  JsonArray currentInputs = doc.createNestedArray("currentInputs");
  for (int i = 0; i < 4; i++) {
    JsonObject input = currentInputs.createNestedObject();
    input["id"] = i;
    input["value"] = currentValues[i];
  }
  
  String response;
  serializeJson(doc, response);
  
  request->send(200, "application/json", response);
  
  debugPrintln("DEBUG: IO status sent to client");
}

// Handler for setting relay states
void handleSetRelay(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("DEBUG: API request received: /api/io/relay");
  
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
      if (state) {
        relayState |= (1 << relay);
      } else {
        relayState &= ~(1 << relay);
      }
      
      debugPrintf("DEBUG: New relay state: 0x%02X\n", relayState);
      request->send(200, "application/json", "{\"status\":\"success\"}");
      return;
    } else {
      debugPrintln("DEBUG: Invalid relay ID");
    }
  } else {
    debugPrintln("DEBUG: Missing relay ID or state");
  }
  
  request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid relay ID or state\"}");
}

// Handler for setting all relay states
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
      relayState = 0;
      for (int i = 0; i < 8; i++) {
        if (states[i].as<bool>()) {
          relayState |= (1 << i);
        }
      }
      
      debugPrintf("DEBUG: New relay state: 0x%02X\n", relayState);
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

// Handler for MODBUS requests
void handleModbusRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  debugPrintln("DEBUG: API request received: /api/modbus/request");
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    debugPrintf("DEBUG: JSON parsing error: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"JSON parsing error\"}");
    return;
  }
  
  uint8_t deviceAddr, functionCode;
  uint16_t startAddr, quantity;
  bool success = false;
  
  if (doc.containsKey("deviceAddr") && doc.containsKey("functionCode") && 
      doc.containsKey("startAddr")) {
      
    deviceAddr = doc["deviceAddr"].as<uint8_t>();
    functionCode = doc["functionCode"].as<uint8_t>();
    startAddr = doc["startAddr"].as<uint16_t>();
    
    debugPrintf("DEBUG: MODBUS request - Device: %d, Function: %d, Start Address: %d\n", 
                deviceAddr, functionCode, startAddr);
    
    // Prepare MODBUS request
    uint8_t requestLength = 0;
    modbusRequestBuffer[requestLength++] = deviceAddr;
    modbusRequestBuffer[requestLength++] = functionCode;
    modbusRequestBuffer[requestLength++] = highByte(startAddr);
    modbusRequestBuffer[requestLength++] = lowByte(startAddr);
    
    // Different handling based on function code
    switch (functionCode) {
      case 0x01: // Read Coils
      case 0x02: // Read Discrete Inputs
      case 0x03: // Read Holding Registers
      case 0x04: // Read Input Registers
        if (doc.containsKey("quantity")) {
          quantity = doc["quantity"].as<uint16_t>();
          debugPrintf("DEBUG: Read request with quantity: %d\n", quantity);
          modbusRequestBuffer[requestLength++] = highByte(quantity);
          modbusRequestBuffer[requestLength++] = lowByte(quantity);
        } else {
          debugPrintln("DEBUG: Missing quantity parameter");
          request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing quantity parameter\"}");
          return;
        }
        break;
        
      case 0x05: // Write Single Coil
        if (doc.containsKey("value")) {
          bool value = doc["value"].as<bool>();
          debugPrintf("DEBUG: Write single coil with value: %d\n", value);
          modbusRequestBuffer[requestLength++] = value ? 0xFF : 0x00;
          modbusRequestBuffer[requestLength++] = 0x00;
        } else {
          debugPrintln("DEBUG: Missing value parameter");
          request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing value parameter\"}");
          return;
        }
        break;
        
      case 0x06: // Write Single Register
        if (doc.containsKey("value")) {
          uint16_t value = doc["value"].as<uint16_t>();
          debugPrintf("DEBUG: Write single register with value: %d\n", value);
          modbusRequestBuffer[requestLength++] = highByte(value);
          modbusRequestBuffer[requestLength++] = lowByte(value);
        } else {
          debugPrintln("DEBUG: Missing value parameter");
          request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing value parameter\"}");
          return;
        }
        break;
        
      case 0x0F: // Write Multiple Coils
      case 0x10: // Write Multiple Registers
        if (doc.containsKey("values")) {
          JsonArray values = doc["values"].as<JsonArray>();
          quantity = values.size();
          
          debugPrintf("DEBUG: Write multiple with %d values\n", quantity);
          
          modbusRequestBuffer[requestLength++] = highByte(quantity);
          modbusRequestBuffer[requestLength++] = lowByte(quantity);
          
          if (functionCode == 0x0F) { // Write Multiple Coils
            uint8_t byteCount = (quantity + 7) / 8;
            modbusRequestBuffer[requestLength++] = byteCount;
            
            uint8_t byteValue = 0;
            uint8_t bitPosition = 0;
            
            for (size_t i = 0; i < quantity; i++) {
              if (values[i].as<bool>()) {
                byteValue |= (1 << bitPosition);
              }
              
              bitPosition++;
              if (bitPosition == 8 || i == quantity - 1) {
                modbusRequestBuffer[requestLength++] = byteValue;
                byteValue = 0;
                bitPosition = 0;
              }
            }
          } else { // Write Multiple Registers
            uint8_t byteCount = quantity * 2;
            modbusRequestBuffer[requestLength++] = byteCount;
            
            for (size_t i = 0; i < quantity; i++) {
              uint16_t value = values[i].as<uint16_t>();
              modbusRequestBuffer[requestLength++] = highByte(value);
              modbusRequestBuffer[requestLength++] = lowByte(value);
            }
          }
        } else {
          debugPrintln("DEBUG: Missing values parameter");
          request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing values parameter\"}");
          return;
        }
        break;
        
      default:
        debugPrintf("DEBUG: Unsupported function code: %d\n", functionCode);
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Unsupported function code\"}");
        return;
    }
    
    // Calculate CRC and add to request
    uint16_t crc = calculateCRC16(modbusRequestBuffer, requestLength);
    modbusRequestBuffer[requestLength++] = lowByte(crc);
    modbusRequestBuffer[requestLength++] = highByte(crc);
    
    // Send MODBUS request and get response
    uint8_t responseLength = 0;
    success = sendModbusRequest(modbusRequestBuffer, requestLength, modbusResponseBuffer, responseLength);
    
    // Prepare response JSON
    DynamicJsonDocument responseDoc(1024);
    responseDoc["success"] = success;
    responseDoc["functionCode"] = functionCode;
    
    if (success) {
      debugPrintln("DEBUG: MODBUS request successful");
      JsonArray data = responseDoc.createNestedArray("data");
      
      // Parse response based on function code
      if (functionCode == 0x01 || functionCode == 0x02) {
        // Read Coils or Discrete Inputs
        uint8_t byteCount = modbusResponseBuffer[2];
        for (uint8_t i = 0; i < byteCount; i++) {
          uint8_t coilByte = modbusResponseBuffer[3 + i];
          for (uint8_t bit = 0; bit < 8; bit++) {
            if (data.size() < quantity) {
              data.add((coilByte & (1 << bit)) != 0);
            }
          }
        }
      } else if (functionCode == 0x03 || functionCode == 0x04) {
        // Read Holding Registers or Input Registers
        uint8_t byteCount = modbusResponseBuffer[2];
        for (uint8_t i = 0; i < byteCount; i += 2) {
          uint16_t regValue = (modbusResponseBuffer[3 + i] << 8) | modbusResponseBuffer[4 + i];
          data.add(regValue);
        }
      } else if (functionCode == 0x05 || functionCode == 0x06) {
        // Write Single Coil or Register
        uint16_t address = (modbusResponseBuffer[2] << 8) | modbusResponseBuffer[3];
        uint16_t value = (modbusResponseBuffer[4] << 8) | modbusResponseBuffer[5];
        data.add(address);
        data.add(value);
      } else if (functionCode == 0x0F || functionCode == 0x10) {
        // Write Multiple Coils or Registers
        uint16_t startAddress = (modbusResponseBuffer[2] << 8) | modbusResponseBuffer[3];
        uint16_t quantity = (modbusResponseBuffer[4] << 8) | modbusResponseBuffer[5];
        data.add(startAddress);
        data.add(quantity);
      }
    } else {
      debugPrintln("DEBUG: MODBUS communication failed");
      responseDoc["error"] = "MODBUS communication failed";
    }
    
    String responseJson;
    serializeJson(responseDoc, responseJson);
    request->send(200, "application/json", responseJson);
    
  } else {
    debugPrintln("DEBUG: Missing required parameters");
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing required parameters\"}");
  }
}

void setup() {
  // Initialize serial in debug mode 
  Serial.begin(115200);
  currentUartMode = UART_MODE_DEBUG;
  delay(500); // Give serial time to initialize
  
  debugPrintln("\n\n-------------------------");
  debugPrintln("ES32A08 Setup Utility Starting...");
  debugPrintln("-------------------------");

  // Initialize SPIFFS
  debugPrintln("DEBUG: Initializing SPIFFS...");
  if(!SPIFFS.begin(true)){
    debugPrintln("ERROR: SPIFFS initialization failed!");
    return;
  }
  debugPrintln("DEBUG: SPIFFS initialized successfully");
  
  // List available files
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

  // Initialize all pins
  debugPrintln("DEBUG: Initializing pins...");
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);
  pinMode(PWR_LED, OUTPUT);
  digitalWrite(PWR_LED, HIGH);
  debugPrintln("DEBUG: Button pins initialized");
  
  // Initialize the 74HC165 (digital inputs)
  pinMode(LOAD_165, OUTPUT);
  pinMode(CLK_165, OUTPUT);
  pinMode(DATA165, INPUT);
  debugPrintln("DEBUG: 74HC165 pins initialized");
  
  // Initialize RS485 direction pin
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW); // Receive mode
  rs485Initialized = true;
  
  // Initialize the shift register (74HC595) with timeout
  debugPrintln("DEBUG: Starting 74HC595 initialization with timeout...");
  initTestModeComplete = false;
  xTaskCreate(initTestModeTask, "InitTask", 4096, NULL, 1, NULL);
  
  // Wait for initialization or timeout
  unsigned long startTime = millis();
  while (!initTestModeComplete && (millis() - startTime < 3000)) {
    delay(100);
    debugPrint(".");
  }
  
  if (initTestModeComplete) {
    debugPrintln("\nDEBUG: 74HC595 initialization completed");
  } else {
    debugPrintln("\nDEBUG: 74HC595 initialization timed out, continuing anyway");
  }
  
  // Try to initialize RS485 once
  debugPrintln("DEBUG: Testing RS485 communication...");
  // Switch UART temporarily to RS485 mode to test
  switchToRS485Mode();
  
  // Simple test of RS485
  rs485Transmit(true);
  Serial.write(0xFF); // Send a test byte
  Serial.flush();
  rs485Transmit(false);
  
  // Wait briefly for any response
  delay(100);
  
  // Switch back to debug mode
  switchToDebugMode();
  debugPrintln("DEBUG: RS485 test completed");
  
  // Set up Access Point
  debugPrintln("DEBUG: Setting up WiFi Access Point...");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  debugPrint("DEBUG: AP IP address: ");
  debugPrintln(IP.toString().c_str());

  // Route for root / web page
  debugPrintln("DEBUG: Setting up web server routes...");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving index.html");
    request->send(SPIFFS, "/index.html", String(), false);
  });
  
  // Route for MODBUS RTU tester page
  server.on("/modbus.html", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving modbus.html");
    request->send(SPIFFS, "/modbus.html", String(), false);
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
  
  server.on("/js/modbus.js", HTTP_GET, [](AsyncWebServerRequest *request){
    debugPrintln("DEBUG: Serving modbus.js");
    request->send(SPIFFS, "/js/modbus.js", "text/javascript");
  });
  
  // API endpoints
  debugPrintln("DEBUG: Setting up API endpoints...");
  server.on("/api/io/status", HTTP_GET, handleGetIOStatus);
  
  server.on("/api/io/relay", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleSetRelay
  );
  
  server.on("/api/io/relays", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleSetAllRelays
  );
  
  server.on("/api/modbus/request", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleModbusRequest
  );
  
  // Start server
  debugPrintln("DEBUG: Starting web server...");
  server.begin();
  debugPrintln("DEBUG: Web server started");
  
  // Create task to update analog values
  debugPrintln("DEBUG: Creating analog update task...");
  xTaskCreate(vAnalogTask, "AnalogTask", 4096, NULL, 1, NULL);
  
  debugPrintln("DEBUG: Setup complete!");
  debugPrintln("-------------------------");
  debugPrintf("Connect to WiFi SSID: %s with password: %s\n", ssid, password);
  debugPrintf("Then navigate to http://%s in your browser\n", IP.toString().c_str());
  debugPrintln("-------------------------");
}

void loop() {
  // Nothing to do here since we're using FreeRTOS tasks
  static uint32_t heartbeatTime = 0;
  if (millis() - heartbeatTime > 10000) {
    heartbeatTime = millis();
    
    // Make sure we're in debug mode for heartbeat messages
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