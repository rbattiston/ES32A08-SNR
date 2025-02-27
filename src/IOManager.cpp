// IOManager.cpp
#include "IOManager.h"
#include "PinConfig.h"
#include "Utils.h"
#include "TestMode.h"

// Global variables for IO state
volatile uint8_t relayState = 0;
volatile bool initTestModeComplete = false;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// IO state arrays
float voltageValues[4] = {0.0, 0.0, 0.0, 0.0};
float currentValues[4] = {0.0, 0.0, 0.0, 0.0};
bool buttonStates[4] = {false, false, false, false};
bool inputStates[8] = {false, false, false, false, false, false, false, false};

void initIOManager() {
  debugPrintln("DEBUG: Initializing IO manager...");
  
  // Initialize button pins
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
  
  // Initialize the shift register (74HC595) with timeout
  debugPrintln("DEBUG: Starting 74HC595 initialization with timeout...");
  initTestModeComplete = false;
  xTaskCreatePinnedToCore(initTestModeTask, "InitTask", 4096, NULL, 1, NULL, 1);
  
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
  
  // Create relay update task
  xTaskCreatePinnedToCore(
    vRelayUpdateTask,
    "RelayTask",
    2048,
    NULL,
    1,
    NULL,
    1
  );
  
  // Create analog update task
  xTaskCreatePinnedToCore(
    vAnalogTask,
    "AnalogTask",
    4096,
    NULL,
    1,
    NULL,
    1
  );
  
  debugPrintln("DEBUG: IO manager initialized");
}

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
    initTestMode();
    debugPrintln("DEBUG: initTestMode completed successfully");
  } catch (...) {
    debugPrintln("DEBUG: Exception in initTestMode");
  }
  
  initTestModeComplete = true;
  vTaskDelete(NULL);
}

void vRelayUpdateTask(void *pvParameters) {
  debugPrintln("DEBUG: Relay update task started");
  
  // Local variable to track relay state changes for debugging
  uint8_t lastRelayState = 0xFF; // Initialize to a different value to force first update
  
  for (;;) {
    // Check if relay state has changed
    if (relayState != lastRelayState) {
      lastRelayState = relayState;
      debugPrintf("DEBUG: Relay state changed to 0x%02X\n", relayState);
    }
    
    // Send the current relay state to the shift register
    // We need to disable interrupts briefly to ensure the shift register operations aren't interrupted
    portENTER_CRITICAL(&mux);
    
    // Send relay state byte directly
    digitalWrite(SH595_LATCH, LOW);
    
    // Send relay state as first byte
    for (uint8_t i = 0; i < 8; i++) {
      digitalWrite(SH595_DATA, (relayState & (0x80 >> i)) ? HIGH : LOW);
      digitalWrite(SH595_CLOCK, LOW);
      digitalWrite(SH595_CLOCK, HIGH);
    }
    
    // Send zero for other bytes (display control) to avoid interference
    for (uint8_t i = 0; i < 16; i++) { // 16 more bits (2 bytes) for display control
      digitalWrite(SH595_DATA, LOW);
      digitalWrite(SH595_CLOCK, LOW);
      digitalWrite(SH595_CLOCK, HIGH);
    }
    
    digitalWrite(SH595_LATCH, HIGH);
    
    portEXIT_CRITICAL(&mux);
    
    // Brief debug message every 10 seconds for monitoring
    static uint32_t lastDebugTime = 0;
    if (millis() - lastDebugTime > 10000) {
      lastDebugTime = millis();
      debugPrintf("DEBUG: Relay update task running, current state: 0x%02X\n", relayState);
    }
    
    // Update at a reasonable rate
    vTaskDelay(pdMS_TO_TICKS(50)); // 50ms delay (20Hz update rate)
  }
}

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
    
    /* try {
      analog_value[2] = analogRead(AI_V3);
    } catch (...) {
      analog_value[2] = 0;
    }
    
    try {
      analog_value[3] = analogRead(AI_V4);
    } catch (...) {
      analog_value[3] = 0;
    } */
    
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

void startRelayTest() {
  xTaskCreatePinnedToCore(
    [](void *pvParameters) {
      debugPrintln("DEBUG: Relay test task started");
      
      // First turn all relays off
      relayState = 0x00;
      vTaskDelay(pdMS_TO_TICKS(500));
      
      // Turn each relay on and off in sequence
      for (int i = 0; i < 8; i++) {
        debugPrintf("DEBUG: Testing relay %d - ON\n", i + 1);
        
        // Turn on this relay
        relayState = (1 << i);
        vTaskDelay(pdMS_TO_TICKS(500));
        
        debugPrintf("DEBUG: Testing relay %d - OFF\n", i + 1);
        
        // Turn off this relay
        relayState = 0x00;
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      
      // Turn all relays on
      debugPrintln("DEBUG: All relays ON");
      relayState = 0xFF;
      vTaskDelay(pdMS_TO_TICKS(1000));
      
      // Turn all relays off
      debugPrintln("DEBUG: All relays OFF");
      relayState = 0x00;
      vTaskDelay(pdMS_TO_TICKS(500));
      
      debugPrintln("DEBUG: Relay test completed");
      
      // Delete this task when finished
      vTaskDelete(NULL);
    },
    "RelayTest",
    2048,
    NULL,
    1,
    NULL,
    1
  );
}

void setRelay(uint8_t relay, bool state) {
  if (relay >= 0 && relay < 8) {
    uint8_t oldState = relayState;
    
    if (state) {
      relayState |= (1 << relay);
    } else {
      relayState &= ~(1 << relay);
    }
    
    debugPrintf("DEBUG: Relay state changed: 0x%02X -> 0x%02X\n", oldState, relayState);
  }
}

void setAllRelays(uint8_t state) {
  relayState = state;
  debugPrintf("DEBUG: All relays set to: 0x%02X\n", state);
}

uint8_t getRelayState() {
  return relayState;
}

bool getButtonState(uint8_t button) {
  if (button < 4) {
    return buttonStates[button];
  }
  return false;
}

bool getInputState(uint8_t input) {
  if (input < 8) {
    return inputStates[input];
  }
  return false;
}

float getVoltageValue(uint8_t channel) {
  if (channel < 4) {
    return voltageValues[channel];
  }
  return 0.0;
}

float getCurrentValue(uint8_t channel) {
  if (channel < 4) {
    return currentValues[channel];
  }
  return 0.0;
}

float* getVoltageValues() {
  return voltageValues;
}

float* getCurrentValues() {
  return currentValues;
}

bool* getButtonStates() {
  return buttonStates;
}

bool* getInputStates() {
  return inputStates;
}