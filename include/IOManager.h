// IOManager.h
#ifndef IO_MANAGER_H
#define IO_MANAGER_H

#include <Arduino.h>
#include "TestMode.h" // Add this include

// Initialize IO manager
void initIOManager();

// Relay functions
void setRelay(uint8_t relay, bool state);
void setAllRelays(uint8_t state);
uint8_t getRelayState();

// Read input values
bool getButtonState(uint8_t button);
bool getInputState(uint8_t input);
float getVoltageValue(uint8_t channel);
float getCurrentValue(uint8_t channel);

// Get array pointers for use in API responses
float* getVoltageValues();
float* getCurrentValues();
bool* getButtonStates();
bool* getInputStates();

// Task for 74HC595 initialization
void initTestModeTask(void *pvParameters);

// Relay update task
void vRelayUpdateTask(void *pvParameters);

// Analog update task
void vAnalogTask(void *pvParameters);

// IO Test functions
void startRelayTest();

// External variables
extern volatile uint8_t relayState;
extern volatile bool initTestModeComplete;
extern portMUX_TYPE mux;

#endif // IO_MANAGER_H