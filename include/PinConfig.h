#ifndef PINCONFIG_H
#define PINCONFIG_H

// ---------------------------------------------------------------------------
// 0-10 V ANALOG INPUTS
// ---------------------------------------------------------------------------
#define AI_V1  32
#define AI_V2  33
// #define AI_V3  25  //Pins 25 and 26 used for WiFi
// #define AI_V4  26

// ---------------------------------------------------------------------------
// 0-20 mA ANALOG INPUTS
// ---------------------------------------------------------------------------
#define AI_I1  36
#define AI_I2  39
#define AI_I3  34
#define AI_I4  35

// ---------------------------------------------------------------------------
// DIGITAL INPUTS via 74HC165 (IN1–IN8)
// Typically needs 3 pins for data (Q7), clock (CP), and latch/load (PL).
// Adjust these if your board schematic shows a different wiring.
// ---------------------------------------------------------------------------
#define SH165_DATA   16  // Serial data output from 74HC165
#define SH165_CLOCK  17  // Shift clock input to 74HC165
#define SH165_LATCH  5   // Parallel load (latch) pin on 74HC165
  // If you're using the 74HC165 for additional digital inputs, you could define:
//#define LOAD_165  16
//#define CLK_165   17
//#define DATA165   5

// ---------------------------------------------------------------------------
// DIGITAL OUTPUTS via 74HC595 (CH1–CH8 + 4-digit display)
// Often requires data (DS), clock (SHCP), latch (STCP), and possibly OE or MR.
// Here we assume DATA=IO13, CLOCK=IO14, LATCH=IO27, and OE=IO4 as examples.
// ---------------------------------------------------------------------------
// 74HC595 for driving the 7-seg display and relay outputs
#define SH595_DATA   13  // Data pin for 74HC595
#define SH595_CLOCK  27  // Clock pin for 74HC595 (matches sample)
#define SH595_LATCH  14  // Latch pin for 74HC595 (matches sample)
#define SH595_OE     4   // Output Enable pin for 74HC595

// ---------------------------------------------------------------------------
// 4x BUTTONS
// ---------------------------------------------------------------------------
#define BTN1  18
#define BTN2  19
#define BTN3  21
#define BTN4  23
#define KEY1  18
#define KEY2  19
#define KEY3  21
#define KEY4  23

// ---------------------------------------------------------------------------
// RS485 INTERFACE
// TX, RX, and Driver Enable (DE) pins for hardware serial
// ---------------------------------------------------------------------------
#define RS485_TX  1
#define RS485_RX  3
#define RS485_DE  22

// ---------------------------------------------------------------------------
// POWER (LED) or STATUS LED
// ---------------------------------------------------------------------------
#define PWR_LED   15


  #define LOAD_165  16
  #define CLK_165   17
  #define DATA165   5

#endif // PINCONFIG_H