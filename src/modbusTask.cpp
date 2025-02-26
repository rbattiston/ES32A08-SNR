#include <Arduino.h>
#include "modbusTask.h"
#include "driver/gpio.h"

// Uncomment the next line to enable debug output
#define DEBUG

#ifdef DEBUG
  #define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
#endif

//-------------------------------------------------------------------------
// Helper Function: Calculate Modbus RTU CRC16 over the provided buffer.
uint16_t calculateCRC(uint8_t *buffer, uint8_t length) {
  uint16_t crc = 0xFFFF;
  for (uint8_t pos = 0; pos < length; pos++) {
    crc ^= (uint16_t)buffer[pos];
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
#ifdef DEBUG
  DEBUG_PRINT("Calculated CRC: 0x");
  if(crc < 0x10) DEBUG_PRINT("0");
  DEBUG_PRINTLN(crc, HEX);
#endif
  return crc;
}

//-------------------------------------------------------------------------
// Helper Function: Send the prepared command buffer over RS485.
void sendModbusCommand(uint8_t *cmd, uint8_t length) {
#ifdef DEBUG
  DEBUG_PRINT("Sending Modbus command: ");
  for(uint8_t i = 0; i < length; i++){
    if(cmd[i] < 0x10) DEBUG_PRINT("0");
    DEBUG_PRINT(cmd[i], HEX);
    DEBUG_PRINT(" ");
  }
  DEBUG_PRINTLN("");
#endif

  // Enable RS485 transmit mode
  digitalWrite(MODBUS_DE_RE_PIN, HIGH);
#ifdef DEBUG
  DEBUG_PRINTLN("RS485 DE/RE set HIGH (TX enabled)");
#endif

  Serial2.write(cmd, length);
  Serial2.flush();

  // Disable RS485 transmit mode
  digitalWrite(MODBUS_DE_RE_PIN, LOW);
#ifdef DEBUG
  DEBUG_PRINTLN("RS485 DE/RE set LOW (TX disabled)");
#endif
}

//-------------------------------------------------------------------------
// 1. Read Coils (Function Code 0x01)
// Query: [Slave][0x01][StartAddr Hi][StartAddr Lo][Quantity Hi][Quantity Lo][CRC Lo][CRC Hi]
void modbusReadCoils(uint8_t slave, uint16_t startAddress, uint16_t quantity) {
  uint8_t cmd[8];
  cmd[0] = slave;
  cmd[1] = 0x01;
  cmd[2] = startAddress >> 8;
  cmd[3] = startAddress & 0xFF;
  cmd[4] = quantity >> 8;
  cmd[5] = quantity & 0xFF;
  uint16_t crc = calculateCRC(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  sendModbusCommand(cmd, 8);
}

//-------------------------------------------------------------------------
// 2. Read Discrete Inputs (Function Code 0x02)
void modbusReadDiscreteInputs(uint8_t slave, uint16_t startAddress, uint16_t quantity) {
  uint8_t cmd[8];
  cmd[0] = slave;
  cmd[1] = 0x02;
  cmd[2] = startAddress >> 8;
  cmd[3] = startAddress & 0xFF;
  cmd[4] = quantity >> 8;
  cmd[5] = quantity & 0xFF;
  uint16_t crc = calculateCRC(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  sendModbusCommand(cmd, 8);
}

//-------------------------------------------------------------------------
// 3. Read Holding Registers (Function Code 0x03)
void modbusReadHoldingRegisters(uint8_t slave, uint16_t startAddress, uint16_t quantity) {
  uint8_t cmd[8];
  cmd[0] = slave;
  cmd[1] = 0x03;
  cmd[2] = startAddress >> 8;
  cmd[3] = startAddress & 0xFF;
  cmd[4] = quantity >> 8;
  cmd[5] = quantity & 0xFF;
  uint16_t crc = calculateCRC(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  sendModbusCommand(cmd, 8);
}

//-------------------------------------------------------------------------
// 4. Read Input Registers (Function Code 0x04)
void modbusReadInputRegisters(uint8_t slave, uint16_t startAddress, uint16_t quantity) {
  uint8_t cmd[8];
  cmd[0] = slave;
  cmd[1] = 0x04;
  cmd[2] = startAddress >> 8;
  cmd[3] = startAddress & 0xFF;
  cmd[4] = quantity >> 8;
  cmd[5] = quantity & 0xFF;
  uint16_t crc = calculateCRC(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  sendModbusCommand(cmd, 8);
}

//-------------------------------------------------------------------------
// 5. Write Single Coil (Function Code 0x05)
// When state is true, the coil is forced ON (0xFF00); otherwise OFF (0x0000).
void modbusWriteSingleCoil(uint8_t slave, uint16_t coilAddress, bool state) {
  uint8_t cmd[8];
  cmd[0] = slave;
  cmd[1] = 0x05;
  cmd[2] = coilAddress >> 8;
  cmd[3] = coilAddress & 0xFF;
  if(state) {
    cmd[4] = 0xFF;
    cmd[5] = 0x00;
  } else {
    cmd[4] = 0x00;
    cmd[5] = 0x00;
  }
  uint16_t crc = calculateCRC(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  sendModbusCommand(cmd, 8);
}

//-------------------------------------------------------------------------
// 6. Write Single Register (Function Code 0x06)
void modbusWriteSingleRegister(uint8_t slave, uint16_t registerAddress, uint16_t value) {
  uint8_t cmd[8];
  cmd[0] = slave;
  cmd[1] = 0x06;
  cmd[2] = registerAddress >> 8;
  cmd[3] = registerAddress & 0xFF;
  cmd[4] = value >> 8;
  cmd[5] = value & 0xFF;
  uint16_t crc = calculateCRC(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  sendModbusCommand(cmd, 8);
}

//-------------------------------------------------------------------------
// 7. Write Multiple Coils (Function Code 0x0F)
// coilData points to a series of bytes (packed LSB-first) representing the coil states.
void modbusWriteMultipleCoils(uint8_t slave, uint16_t startAddress, uint16_t quantity, uint8_t *coilData) {
  uint8_t byteCount = (quantity + 7) / 8;
  uint8_t cmdLength = 7 + byteCount + 2;  // header (7 bytes) + data bytes + CRC (2 bytes)
  uint8_t cmd[256];  // Adjust size as needed

  cmd[0] = slave;
  cmd[1] = 0x0F;
  cmd[2] = startAddress >> 8;
  cmd[3] = startAddress & 0xFF;
  cmd[4] = quantity >> 8;
  cmd[5] = quantity & 0xFF;
  cmd[6] = byteCount;
  for(uint8_t i = 0; i < byteCount; i++){
    cmd[7 + i] = coilData[i];
  }
  uint16_t crc = calculateCRC(cmd, 7 + byteCount);
  cmd[7 + byteCount] = crc & 0xFF;
  cmd[7 + byteCount + 1] = (crc >> 8) & 0xFF;
  sendModbusCommand(cmd, cmdLength);
}

//-------------------------------------------------------------------------
// 8. Write Multiple Registers (Function Code 0x10)
// values points to an array of 16-bit register values to be written (big-endian in the message).
void modbusWriteMultipleRegisters(uint8_t slave, uint16_t startAddress, uint16_t quantity, uint16_t *values) {
  uint8_t byteCount = quantity * 2;  // each register is 2 bytes
  uint8_t cmdLength = 7 + byteCount + 2;  // header (7) + data bytes + CRC (2)
  uint8_t cmd[256];  // Adjust size as needed

  cmd[0] = slave;
  cmd[1] = 0x10;
  cmd[2] = startAddress >> 8;
  cmd[3] = startAddress & 0xFF;
  cmd[4] = quantity >> 8;
  cmd[5] = quantity & 0xFF;
  cmd[6] = byteCount;
  // Insert each register's value (high byte first)
  for(uint16_t i = 0; i < quantity; i++){
    cmd[7 + i*2] = values[i] >> 8;
    cmd[7 + i*2 + 1] = values[i] & 0xFF;
  }
  uint16_t crc = calculateCRC(cmd, 7 + byteCount);
  cmd[7 + byteCount] = crc & 0xFF;
  cmd[7 + byteCount + 1] = (crc >> 8) & 0xFF;
  sendModbusCommand(cmd, cmdLength);
}

//-------------------------------------------------------------------------
// Example Task: Demonstrate one or more Modbus operations.
// In this example, every second the task toggles 8 coils on device address 2,
// starting at coil address 1, using Write Multiple Coils (0x0F).
void modbusScannerTask(void *parameter) {
  // Set up RS485 control pin.
  pinMode(MODBUS_DE_RE_PIN, OUTPUT);
  digitalWrite(MODBUS_DE_RE_PIN, LOW);

  // Initialize Serial2 for RS485 communication.
  Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  vTaskDelay(1000 / portTICK_PERIOD_MS);

#ifdef DEBUG
  DEBUG_PRINTLN("Starting modbus scanner task...");
#endif

  // Example variable to hold coil state (8 coils in one byte)
  uint8_t coilState = 0x00;

  for (;;) {
#ifdef DEBUG
    DEBUG_PRINTLN("Toggling 8 coils on device 2, starting at coil address 1...");
#endif
    // Toggle the state of all 8 coils.
    coilState = ~coilState;
#ifdef DEBUG
    DEBUG_PRINT("New coil state: 0x");
    if(coilState < 0x10) DEBUG_PRINT("0");
    DEBUG_PRINTLN(coilState, HEX);
#endif
    // Send Write Multiple Coils command:
    // - Slave: 2, Start Address: 1, Quantity: 8, Data: coilState packed in one byte.
    modbusWriteMultipleCoils(2, 1, 8, &coilState);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
