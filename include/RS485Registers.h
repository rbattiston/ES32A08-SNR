#ifndef RS485REGISTERS_H
#define RS485REGISTERS_H

// Structure representing a block of registers on a device.
typedef struct {
  uint16_t startAddress;    // Starting register address
  uint16_t registerCount;   // Number of registers in this block
  const char* description;  // Description of what this block controls
} RS485RegisterBlock;

// Waveshare 8ch Relay Register Blocks
// (The following addresses and descriptions are based on the XML table data.)
static RS485RegisterBlock waveshareRelayRegisterBlocks[] = {
  // The relay control registers for channels 1-8:
  {0x0000, 8, "Relay channel control (each channel: 0xFF00 = On; 0x0000 = Off; 0x5500 = Toggle)"},
  // Register for operating all relays:
  {0x00FF, 1, "Operate all relays (0xFF00 = All On; 0x0000 = All Off; 0x5500 = Toggle)"},
  // Toggle registers for individual channels:
  {0x0100, 8, "Toggle relays for channels 1-8 (Write: 0x05 or 0x0F)"},
  // Toggle register for all relays:
  {0x01FF, 1, "Toggle all relays (Write: 0x05)"},
  // Flash open registers for channels (delay = data * 100ms):
  {0x0200, 8, "Flash open for relays (channel 1-8, delay = data * 100ms)"},
  {0x0400, 8, "Flash open for relays (channel 1-8, delay = data * 100ms)"},
  // Serial port parameters (4x2000): 
  {0x2000, 4, "Serial port parameters (High byte: checksum method 0x00~0x02; Low byte: baud rate method 0x00~0x07)"},
  // Device address registers (4x4000):
  {0x4000, 4, "Device address (stores Modbus address 0x0001-0x00FF)"},
  // Software version registers (4x8000):
  {0x8000, 4, "Software version (Convert to decimal, shift two places left, e.g. 0x0064 -> 100 -> V1.00)"}
};

static const uint8_t waveshareRelayRegisterBlockCount = sizeof(waveshareRelayRegisterBlocks) / sizeof(RS485RegisterBlock);

#endif // RS485REGISTERS_H
