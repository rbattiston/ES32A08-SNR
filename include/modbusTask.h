// modbusTask.h
#ifndef MODBUS_TASK_H
#define MODBUS_TASK_H

#include <Arduino.h>

#define MODBUS_DE_RE_PIN 22   // Control pin for DE & RE on MAX485
#define RS485_RX_PIN     3   // ESP32 RX -> MAX485 RO
#define RS485_TX_PIN     1   // ESP32 TX -> MAX485 DI

extern String modbusScanOutput;
extern bool relayOverrides[8];  // Declare the global override array

void modbusScannerTask(void *parameter);
uint16_t calculateCRC(uint8_t *buffer, uint8_t length);
void sendModbusWriteCommand(uint8_t slave, bool coilOn);
void sendModbusWriteCommandForChannel(uint8_t channel, bool state);

#endif // MODBUS_TASK_H
