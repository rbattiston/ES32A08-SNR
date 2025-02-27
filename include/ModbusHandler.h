// ModbusHandler.h
#ifndef MODBUS_HANDLER_H
#define MODBUS_HANDLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h> // Add this include 

// Initialize MODBUS handler
void initModbusHandler();

// MODBUS communication functions
bool sendModbusRequest(uint8_t* request, uint8_t requestLength, uint8_t* response, uint8_t& responseLength);
uint16_t calculateCRC16(uint8_t* buffer, uint8_t length);

// RS485 control
void rs485Transmit(bool enable);

// Handler functions
void handleModbusRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// Constants
#define MODBUS_BUFFER_SIZE 256

// External variables
extern uint8_t modbusRequestBuffer[MODBUS_BUFFER_SIZE];
extern uint8_t modbusResponseBuffer[MODBUS_BUFFER_SIZE];
extern bool rs485Initialized;

#endif // MODBUS_HANDLER_H