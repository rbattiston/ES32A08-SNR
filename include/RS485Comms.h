#ifndef RS485COMMS_H
#define RS485COMMS_H

#include <Arduino.h>

// Structure representing a device discovered on the RS485 bus.
typedef struct {
  uint8_t deviceAddress;     // Current Modbus address (1–255)
  const char* deviceName;    // For example, "Waveshare 8ch Relay"
  uint16_t deviceAddressReg; // Value read from the device address register (expected 1-255)
  uint16_t softwareVersion;  // Software version (e.g., 100 for V1.00)
} RS485Device;

#define MAX_RS485_DEVICES 10

extern RS485Device rs485Devices[MAX_RS485_DEVICES];
extern uint8_t rs485DeviceCount;

// RS485 communications functions:
void initRS485Comms();
void scanRS485Bus();
bool readdressDevice(uint8_t oldAddress, uint8_t newAddress);

// RS485 task that periodically polls devices.
void rs485Task(void *pvParameters);

#endif // RS485COMMS_H
