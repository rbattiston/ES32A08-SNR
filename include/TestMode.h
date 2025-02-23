#ifndef TESTMODE_H
#define TESTMODE_H

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

#endif // TESTMODE_H
