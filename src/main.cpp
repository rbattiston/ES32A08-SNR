#include <Arduino.h>
#include "PinConfig.h"
#include "TestMode.h"
#include "RS485Comms.h"


// Toggle these booleans as desired.
bool sensorTestMode = false;   // Enable sensor test mode?
bool displayTestMode  = false;   // Enable display test mode (counter and 7-seg refresh)?
bool relayTestMode    = false;   // Enable relay test mode (button processing)?
bool diTestMode       = false;   // Enable digital input (DI) test mode?

// Sensor Task: runs sensorTestLoop repeatedly.
void vSensorTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    sensorTestLoop();
    // sensorTestLoop includes a 1-second delay.
  }
}

// Display Task: refreshes the 7-seg display and updates the counter.
void vDisplayTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    testLoop();  // Updates counter and calls TubeDisplayCounter().
    vTaskDelay(2 / portTICK_PERIOD_MS);
  }
}

// Relay Task: updates relayState based on button presses.
void vRelayTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    updateRelayState();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// Digital Input Task: reads DI status via the 74HC165 and prints it.
void vDITask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    diTestLoop();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Device Starting...");

  // Create the RS485 task.
  xTaskCreate(rs485Task, "RS485Task", 4096, NULL, 1, NULL);

  // Initialize keys for relay test.
  pinMode(KEY1, INPUT_PULLUP);
  pinMode(KEY2, INPUT_PULLUP);
  pinMode(KEY3, INPUT_PULLUP);
  pinMode(KEY4, INPUT_PULLUP);
  pinMode(PWR_LED, OUTPUT);

  // Initialize analog inputs for sensor test.
  sensorTestInit();

  // Initialize the display (shift register) for display mode.
  initTestMode();
  
  // Create tasks as desired.
  if (displayTestMode) {
    xTaskCreate(vDisplayTask, "DisplayTask", 2048, NULL, 1, NULL);
  }
  if (relayTestMode) {
    xTaskCreate(vRelayTask, "RelayTask", 2048, NULL, 1, NULL);
  }
  if (sensorTestMode) {
    xTaskCreate(vSensorTask, "SensorTask", 2048, NULL, 1, NULL);
  }
  if (diTestMode) {
    // Initialize DI chain pins.
    pinMode(LOAD_165, OUTPUT);
    pinMode(CLK_165, OUTPUT);
    pinMode(DATA165, INPUT);
    xTaskCreate(vDITask, "DITask", 2048, NULL, 1, NULL);
  }
}

void loop() {
  // Main loop yields to the FreeRTOS scheduler.
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}
