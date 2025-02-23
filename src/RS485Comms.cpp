#include "RS485Comms.h"
#include "RS485Registers.h"
#include <ModbusMaster.h>

// RS485 hardware definitions (adjust pins as needed)
#define RS485_TX_PIN 1
#define RS485_RX_PIN 3
#define RS485_DE_PIN 22

// Create a ModbusMaster instance.
ModbusMaster modbus;

// Global table of discovered devices.
RS485Device rs485Devices[MAX_RS485_DEVICES];
uint8_t rs485DeviceCount = 0;

// Expected device criteria: For Waveshare relay, we assume that reading the Device Address register (0x4000)
// returns a value between 1 and 255, and the software version register (0x8000) returns a nonzero value.
#define EXPECTED_MIN_DEVICE_ADDRESS 1
#define EXPECTED_MAX_DEVICE_ADDRESS 255

// Pre- and post-transmission functions for RS485.
void preTransmission() {
  digitalWrite(RS485_DE_PIN, HIGH);
}
void postTransmission() {
  digitalWrite(RS485_DE_PIN, LOW);
}

void initRS485Comms() {
  pinMode(RS485_DE_PIN, OUTPUT);
  digitalWrite(RS485_DE_PIN, LOW);
  
  Serial2.begin(9600, SERIAL_8N1);
  
  // Initialize modbus instance with a dummy address.
  modbus.begin(1, Serial2);
  modbus.preTransmission(preTransmission);
  modbus.postTransmission(postTransmission);
  
  Serial.println("RS485 communications initialized.");
}

void scanRS485Bus() {
  rs485DeviceCount = 0;
  Serial.println("Starting RS485 bus scan (addresses 1 to 255)...");
  
  // Iterate over addresses 1 to 255.
  for (uint16_t address = 1; address <= 255; address++) {
    Serial.print("Scanning address ");
    Serial.print(address);
    Serial.print("... ");
    
    modbus.begin(address, Serial2);
    // Attempt to read one register at 0x4000 (device address register).
    uint8_t result = modbus.readHoldingRegisters(0x4000, 1);
    
    if (result == modbus.ku8MBSuccess) {
      uint16_t devAddr = modbus.getResponseBuffer(0);
      Serial.print("Response received, DeviceReg = ");
      Serial.print(devAddr);
      if (devAddr >= EXPECTED_MIN_DEVICE_ADDRESS && devAddr <= EXPECTED_MAX_DEVICE_ADDRESS) {
        // Now read software version at register 0x8000.
        result = modbus.readHoldingRegisters(0x8000, 1);
        if (result == modbus.ku8MBSuccess) {
          uint16_t swVersion = modbus.getResponseBuffer(0);
          Serial.print(", SW Version = ");
          Serial.print(swVersion);
          if (swVersion != 0) { // A nonzero version is assumed valid.
            if (rs485DeviceCount < MAX_RS485_DEVICES) {
              rs485Devices[rs485DeviceCount].deviceAddress = address;
              rs485Devices[rs485DeviceCount].deviceAddressReg = devAddr;
              rs485Devices[rs485DeviceCount].softwareVersion = swVersion;
              rs485Devices[rs485DeviceCount].deviceName = "Waveshare 8ch Relay";
              rs485DeviceCount++;
              Serial.println(" -> Device accepted.");
            } else {
              Serial.println(" -> Device found but device table full.");
            }
          } else {
            Serial.println(" -> Invalid software version (0), skipping.");
          }
        } else {
          Serial.println(" -> Failed to read software version, skipping.");
        }
      } else {
        Serial.println(" -> DeviceReg out of range, skipping.");
      }
    } else {
      Serial.println("No response.");
    }
    
    delay(5); // Small delay between scans.
  }
  
  Serial.print("RS485 bus scan complete. ");
  Serial.print(rs485DeviceCount);
  Serial.println(" device(s) found.");
}

bool readdressDevice(uint8_t oldAddress, uint8_t newAddress) {
  Serial.print("Attempting to readdress device at ");
  Serial.print(oldAddress);
  Serial.print(" to ");
  Serial.print(newAddress);
  Serial.println("...");
  
  modbus.begin(oldAddress, Serial2);
  uint8_t result = modbus.writeSingleRegister(0x4000, newAddress);
  if (result == modbus.ku8MBSuccess) {
    delay(100);
    result = modbus.readHoldingRegisters(0x4000, 1);
    if (result == modbus.ku8MBSuccess) {
      uint16_t addrRead = modbus.getResponseBuffer(0);
      if (addrRead == newAddress) {
        Serial.println("Readdress successful.");
        return true;
      }
    }
  }
  Serial.println("Readdress failed.");
  return false;
}

void rs485Task(void *pvParameters) {
  (void)pvParameters;
  
  initRS485Comms();
  scanRS485Bus();
  
  Serial.printf("Found %d RS485 device(s):\n", rs485DeviceCount);
  for (uint8_t i = 0; i < rs485DeviceCount; i++) {
    Serial.printf("Device %d: Addr=%d, DeviceReg=%d, SW Version=%d, Name=%s\n",
                  i,
                  rs485Devices[i].deviceAddress,
                  rs485Devices[i].deviceAddressReg,
                  rs485Devices[i].softwareVersion,
                  rs485Devices[i].deviceName);
  }
  
  // Example: Readdress any device not at address 10.
  for (uint8_t i = 0; i < rs485DeviceCount; i++) {
    if (rs485Devices[i].deviceAddress != 10) {
      bool success = readdressDevice(rs485Devices[i].deviceAddress, 10);
      if (success) {
        Serial.printf("Device at address %d readdressed to 10\n", rs485Devices[i].deviceAddress);
        rs485Devices[i].deviceAddress = 10;
      } else {
        Serial.printf("Failed to readdress device at address %d\n", rs485Devices[i].deviceAddress);
      }
    }
  }
  
  // Poll devices in a loop.
  for (;;) {
    for (uint8_t i = 0; i < rs485DeviceCount; i++) {
      modbus.begin(rs485Devices[i].deviceAddress, Serial2);
      // Example: read 8 registers from 0x0000 representing relay status.
      uint8_t result = modbus.readHoldingRegisters(0x0000, 8);
      if (result == modbus.ku8MBSuccess) {
        Serial.printf("Device Addr %d Relay Status: ", rs485Devices[i].deviceAddress);
        for (uint8_t j = 0; j < 8; j++) {
          Serial.printf("0x%04X ", modbus.getResponseBuffer(j));
        }
        Serial.println();
      } else {
        Serial.printf("Device Addr %d did not respond properly.\n", rs485Devices[i].deviceAddress);
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}
