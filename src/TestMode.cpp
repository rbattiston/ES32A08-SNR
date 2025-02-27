#include <Arduino.h>
#include "TestMode.h"
#include "PinConfig.h"

// Enable debug prints by defining DEBUG.
// #define DEBUG

#ifdef DEBUG
  #define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
#endif

// If not defined in PinConfig.h, define analog input pins:
#ifndef Vi1
  #define Vi1   32
  #define Vi2   33
  #define Vi3   25
  #define Vi4   26
  #define Ii1   36
  #define Ii2   39
  #define Ii3   34
  #define Ii4   35
#endif

// If not defined, define the keys and LED for relay testing:
#ifndef KEY1
  #define KEY1    18
  #define KEY2    19
  #define KEY3    21
  #define KEY4    23
  #define PWR_LED 15
#endif

// If not defined, define DI chip pins for 74HC165:
#ifndef LOAD_165
  #define LOAD_165  16
  #define CLK_165   17
  #define DATA165   5
#endif

//---------------------------------------------------------------------
// Lookup tables for the 7-seg display (common anode)
static uint8_t TUBE_SEG[] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
  0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x58, 0x5E, 0x79,
  0x71, 0x76, 0x74, 0x38, 0x54, 0x37, 0x5C, 0x73,
  0x50, 0x78, 0x3E, 0x40, 0x00
};

static uint8_t TUBE_NUM[8] = {
  0xFE, 0xFF, 0xFD, 0xFF, 0xFB, 0xFF, 0xF7, 0xFF,
};

//---------------------------------------------------------------------
// Global variables for display multiplexing and counting
//---------------------------------------------------------------------
static uint8_t dat;     // Digit value (0–9) to display on the current digit
static uint8_t com_num; // Current digit select index

static unsigned int counter = 0;           // Display counter (0–9999)
static unsigned long lastCounterUpdate = 0;  // Timestamp for counter update

// Global variable for relay state (each bit corresponds to one relay)
// Use the variable defined in IOManager.cpp
extern volatile uint8_t relayState;

//---------------------------------------------------------------------
// Shift Register Functions for 74HC595 (common to display & relay)
//---------------------------------------------------------------------
// Sends one byte out to the 74HC595.
// A debug print shows the value being sent.
void Send_Bytes(uint8_t value) {
  DEBUG_PRINT("Send_Bytes: Sending byte 0x");
  if (value < 0x10) DEBUG_PRINT("0");
  DEBUG_PRINTLN(value, HEX);
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(SH595_DATA, (value & 0x80) ? HIGH : LOW);
    value <<= 1;
    digitalWrite(SH595_CLOCK, LOW);
    digitalWrite(SH595_CLOCK, HIGH);
  }
}

// Combined function: sends three bytes to the 74HC595:
// the relay state, the digit select byte, and the segment data byte.
void Send_74HC595(uint8_t relayOut) {
  uint8_t tube_dat = TUBE_SEG[dat];
  uint8_t bit_num  = TUBE_NUM[com_num];
  DEBUG_PRINT("Send_74HC595: relayOut = 0x");
  if (relayOut < 0x10) DEBUG_PRINT("0");
  DEBUG_PRINT(relayOut, HEX);
  DEBUG_PRINT(", bit_num = 0x");
  if (bit_num < 0x10) DEBUG_PRINT("0");
  DEBUG_PRINT(bit_num, HEX);
  DEBUG_PRINT(", tube_dat = 0x");
  if (tube_dat < 0x10) DEBUG_PRINT("0");
  DEBUG_PRINTLN(tube_dat, HEX);
  
  Send_Bytes(relayOut);  // Send relay state byte
  Send_Bytes(bit_num);   // Send digit select byte
  Send_Bytes(tube_dat);  // Send segment data byte
  
  digitalWrite(SH595_LATCH, LOW);
  digitalWrite(SH595_LATCH, HIGH);
  DEBUG_PRINTLN("Send_74HC595: Latch toggled to update outputs.");
}

//---------------------------------------------------------------------
// Display Task Functions
//---------------------------------------------------------------------
// Multiplexes the 4-digit display to show the current counter value.
void TubeDisplayCounter(unsigned int cnt) {
  uint8_t digit[4];
  digit[0] = (cnt / 1000) % 10;
  digit[1] = (cnt / 100) % 10;
  digit[2] = (cnt / 10) % 10;
  digit[3] = cnt % 10;
  
  DEBUG_PRINT("TubeDisplayCounter: Displaying counter ");
  DEBUG_PRINTLN(cnt);
  DEBUG_PRINT("Digits: ");
  for (uint8_t i = 0; i < 4; i++) {
    DEBUG_PRINT(digit[i]);
    DEBUG_PRINT(" ");
  }
  DEBUG_PRINTLN("");

  // com_num runs from 0 to 7; each pair of iterations uses the same digit.
  for (com_num = 0; com_num < 8; com_num++) {
    dat = digit[com_num / 2];  // Maps: 0,0,1,1,2,2,3,3
    Send_74HC595(relayState);  // Output current relayState as first byte
    delay(2);                  // Short delay for persistence of vision
  }
}

// testLoop: Updates the counter once per second and refreshes the display.
void testLoop() {
  unsigned long now = millis();
  if (now - lastCounterUpdate >= 1000) {
    lastCounterUpdate = now;
    counter = (counter + 1) % 10000;
    DEBUG_PRINT("testLoop: Counter updated to ");
    DEBUG_PRINTLN(counter);
  }
  TubeDisplayCounter(counter);
}

// initTestMode: Initializes the shift register pins for display mode.
void initTestMode() {
  pinMode(SH595_DATA, OUTPUT);
  pinMode(SH595_CLOCK, OUTPUT);
  pinMode(SH595_LATCH, OUTPUT);
  pinMode(SH595_OE, OUTPUT);
  DEBUG_PRINTLN("initTestMode: Initializing shift register pins.");
  // Clear display
  Send_74HC595(0);
  digitalWrite(SH595_OE, LOW);  // Enable outputs (active LOW)
}

//---------------------------------------------------------------------
// Relay Task Functions
//---------------------------------------------------------------------
// updateRelayState: Uses edge detection with debounce to update relayState.
void updateRelayState() {
  const unsigned long debounceDelay = 200;  // 200ms debounce period
  unsigned long now = millis();
  
  bool key1 = (digitalRead(KEY1) == LOW);
  bool key2 = (digitalRead(KEY2) == LOW);
  bool key3 = (digitalRead(KEY3) == LOW);
  bool key4 = (digitalRead(KEY4) == LOW);
  
  static bool lastKey1 = false;
  static bool lastKey2 = false;
  static bool lastKey3 = false;
  static bool lastKey4 = false;
  
  static unsigned long lastToggleTime1 = 0;
  static unsigned long lastToggleTime2 = 0;
  static unsigned long lastToggleTime3 = 0;
  static unsigned long lastToggleTime4 = 0;
  
  if (key1 && (!lastKey1) && (now - lastToggleTime1 >= debounceDelay)) {
    relayState ^= 0x01;
    lastToggleTime1 = now;
    DEBUG_PRINTLN("updateRelayState: KEY1 pressed. Toggling relayState bit 0.");
  }
  if (key2 && (!lastKey2) && (now - lastToggleTime2 >= debounceDelay)) {
    relayState ^= 0x02;
    lastToggleTime2 = now;
    DEBUG_PRINTLN("updateRelayState: KEY2 pressed. Toggling relayState bit 1.");
  }
  if (key3 && (!lastKey3) && (now - lastToggleTime3 >= debounceDelay)) {
    relayState ^= 0x04;
    lastToggleTime3 = now;
    DEBUG_PRINTLN("updateRelayState: KEY3 pressed. Toggling relayState bit 2.");
  }
  if (key4 && (!lastKey4) && (now - lastToggleTime4 >= debounceDelay)) {
    relayState ^= 0x08;
    lastToggleTime4 = now;
    DEBUG_PRINTLN("updateRelayState: KEY4 pressed. Toggling relayState bit 3.");
  }
  
  lastKey1 = key1;
  lastKey2 = key2;
  lastKey3 = key3;
  lastKey4 = key4;
  
  // Indicate relay update activity via LED.
  digitalWrite(PWR_LED, HIGH);
}

//---------------------------------------------------------------------
// Digital Input (DI) Test Functions
//---------------------------------------------------------------------
// Reads 8 DI values via the 74HC165 and returns a byte.
uint8_t Read_74HC165() {
  uint8_t i;
  uint8_t Temp = 0;
  digitalWrite(LOAD_165, LOW);
  digitalWrite(LOAD_165, HIGH);
  for (i = 0; i < 8; i++) {
    Temp <<= 1;
    digitalWrite(CLK_165, LOW);
    if (digitalRead(DATA165) == 0) {  // Active LOW indicates active input.
      Temp |= 0x01;
    }
    digitalWrite(CLK_165, HIGH);
  }
  DEBUG_PRINT("Read_74HC165: Value read = 0x");
  if (Temp < 0x10) DEBUG_PRINT("0");
  DEBUG_PRINTLN(Temp, HEX);
  return Temp;
}

// Reads the DI value twice for consistency.
uint8_t Get_DI_Value() {
  uint8_t Value1 = Read_74HC165();
  delay(20);
  uint8_t Value2 = Read_74HC165();
  if (Value1 == Value2) {
    return Value1;
  }
  DEBUG_PRINTLN("Get_DI_Value: Mismatch between reads; returning 0.");
  return 0; // Alternatively, return Value1
}

// diTestLoop: Reads the DI status and prints it to Serial.
void diTestLoop() {
  DEBUG_PRINTLN("diTestLoop: Reading DI values.");
  uint8_t diStatus = Get_DI_Value();
  Serial.printf("DI Status: 0x%02X\n", diStatus);
  delay(1000);
}

//---------------------------------------------------------------------
// Sensor Test Functions
//---------------------------------------------------------------------
// sensorTestInit: Initializes analog input pins for sensor testing.
void sensorTestInit() {
  pinMode(Vi1, INPUT);
  pinMode(Vi2, INPUT);
  pinMode(Vi3, INPUT);
  pinMode(Vi4, INPUT);
  pinMode(Ii1, INPUT);
  pinMode(Ii2, INPUT);
  pinMode(Ii3, INPUT);
  pinMode(Ii4, INPUT);
  pinMode(PWR_LED, OUTPUT);
  digitalWrite(PWR_LED, LOW);
  DEBUG_PRINTLN("sensorTestInit: Sensor pins initialized.");
}

// sensorTestLoop: Reads 8 analog inputs, converts the values, and prints them.
void sensorTestLoop() {
  int analog_value[8];
  float in_value[8];

  analog_value[0] = analogRead(Vi1);
  analog_value[1] = analogRead(Vi2);
  analog_value[2] = analogRead(Vi3);
  analog_value[3] = analogRead(Vi4);
  analog_value[4] = analogRead(Ii1);
  analog_value[5] = analogRead(Ii2);
  analog_value[6] = analogRead(Ii3);
  analog_value[7] = analogRead(Ii4);

  DEBUG_PRINT("sensorTestLoop: Analog values: ");
  for (int i = 0; i < 8; i++) {
      DEBUG_PRINT(analog_value[i]);
      DEBUG_PRINT(" ");
  }
  DEBUG_PRINTLN("");

  in_value[0] = (float)analog_value[0] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
  in_value[1] = (float)analog_value[1] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
  in_value[2] = (float)analog_value[2] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
  in_value[3] = (float)analog_value[3] * 3300 / 4096 / 1000 * 53 / 10 + 0.6;
  in_value[4] = ((float)analog_value[4] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
  in_value[5] = ((float)analog_value[5] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
  in_value[6] = ((float)analog_value[6] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;
  in_value[7] = ((float)analog_value[7] * 3300 / 4096 / 1000 + 0.12) / 91 * 1000;

  Serial.printf("V1=%.2fV, V2=%.2fV, V3=%.2fV, V4=%.2fV, I1=%.2fmA, I2=%.2fmA, I3=%.2fmA, I4=%.2fmA\n",
                in_value[0], in_value[1], in_value[2], in_value[3],
                in_value[4], in_value[5], in_value[6], in_value[7]);
  delay(1000);
}
