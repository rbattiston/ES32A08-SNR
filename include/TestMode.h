#ifndef TESTMODE_H
#define TESTMODE_H

// Use the extern keyword to indicate that relayState is defined elsewhere
extern volatile uint8_t relayState;

// Display test routines
void initTestMode();
void testLoop();

// Relay test routines
void initRelayTest();
void updateRelayState();

// Sensor test routines
void sensorTestInit();
void sensorTestLoop();

// Digital Input (DI) test routines
void diTestLoop();

// Add these declarations
uint8_t Get_DI_Value();
uint8_t Read_74HC165();

#endif // TESTMODE_H