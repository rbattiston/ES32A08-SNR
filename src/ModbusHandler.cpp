// ModbusHandler.cpp
#include "ModbusHandler.h"
#include "Utils.h"
#include "PinConfig.h"
#include <ArduinoJson.h>

uint8_t modbusRequestBuffer[MODBUS_BUFFER_SIZE];
uint8_t modbusResponseBuffer[MODBUS_BUFFER_SIZE];
bool rs485Initialized = false;

void initModbusHandler() {
  debugPrintln("DEBUG: Initializing MODBUS handler...");
  
  // Initialize RS485 direction pin
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW); // Default to receive mode
  rs485Initialized = true;
  
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
  debugPrintln("DEBUG: MODBUS handler initialized");
}

void rs485Transmit(bool enable) {
  digitalWrite(RS485_DE, enable);
  delay(1); // Short delay for line settling
}

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