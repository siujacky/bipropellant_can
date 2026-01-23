# CAN Controller Mode - Debug & Diagnostics Features

## Overview
Enhanced CAN controller mode with comprehensive debug output, status monitoring, and message decoding for the MCP2515 CAN module.

## Features Added

### 1. MCP2515 Detection Messages
**Location:** `src/can_bus.c` - `CAN_AutoDetectAndInit()`

When the board starts up in CAN controller mode, it now prints:
```
=== CAN Bus Initialization ===
Detecting MCP2515 module...
MCP2515 DETECTED!
Initializing CAN at 250 kbps...
CAN bus initialized successfully
Enabling software serial on PA13/PA14...
CAN controller mode ACTIVE
```

Or if not detected:
```
MCP2515 NOT DETECTED
```

### 2. TX/RX Packet Counters
**Location:** `src/can_bus.c` - `can_stats` structure

Tracks:
- `rx_total` - Total received packets
- `rx_pwm` - PWM command packets
- `rx_speed` - Speed command packets  
- `rx_position` - Position command packets
- `rx_enable` - Enable/disable packets
- `rx_unknown` - Unknown/unmatched IDs
- `tx_total` - Total transmitted packets
- `tx_status_speed` - Speed status packets sent
- `tx_status_pos` - Position status packets sent
- `tx_status_batt` - Battery status packets sent
- `errors` - Error count
- `last_error_flags` - Last error flags value

### 3. CAN Message Decoding & Logging
**Location:** `src/can_bus.c` - `CAN_ProcessMessages()`

#### RX Messages
Every received CAN message is decoded and logged:
```
[CAN RX] ID:0x100 DLC:8 Data: 00 01 02 03 04 05 06 07 [PWM CMD] PWM1:100 PWM2:200
[CAN RX] ID:0x101 DLC:8 Data: 64 00 00 00 C8 00 00 00 [SPEED CMD] SPD1:100 SPD2:200 mm/s
[CAN RX] ID:0x102 DLC:8 Data: E8 03 00 00 D0 07 00 00 [POS CMD] POS1:1000 POS2:2000 mm
[CAN RX] ID:0x103 DLC:1 Data: 01 [ENABLE CMD] EN:1
[CAN RX] ID:0x999 DLC:4 Data: AA BB CC DD [UNKNOWN]
```

#### TX Messages (verbose mode every 1 second)
```
[CAN TX] Speed: L=150 R=155 mm/s
[CAN TX] Position: L=12500 R=12650 mm
[CAN TX] Battery: 36500 mV, Temp: 25 C
```

### 4. Error Flag Monitoring
**Location:** `src/can_bus.c` - `CAN_CheckErrors()`

Checks MCP2515 error flags every 100ms and reports:
```
[CAN ERROR] Flags: 0x05 TXBO TXEP
```

Error flags decoded:
- `RX1OVR` - RX Buffer 1 overflow
- `RX0OVR` - RX Buffer 0 overflow
- `TXBO` - Bus-off error
- `TXEP` - Transmit error passive
- `RXEP` - Receive error passive
- `TXWAR` - Transmit error warning
- `RXWAR` - Receive error warning
- `EWARN` - Error warning

### 5. Periodic Statistics Report
**Location:** `src/can_bus.c` - `CAN_PrintStatistics()`

Automatically prints every 10 seconds:
```
=== CAN Statistics ===
RX Total:     152
  PWM:        50
  Speed:      50
  Position:   50
  Enable:     2
  Unknown:    0

TX Total:     300
  Speed:      100
  Position:   100
  Battery:    100

Errors:       0
======================
```

## API Functions

### Diagnostics Functions
```c
void CAN_PrintStatistics(void);  // Print current statistics
void CAN_ResetStatistics(void);  // Reset all counters
void CAN_CheckErrors(void);      // Check and report MCP2515 errors
```

### Existing Functions (Enhanced)
```c
bool CAN_AutoDetectAndInit(void);      // Now with verbose debug output
void CAN_ProcessMessages(void);        // Now logs and decodes all messages
void CAN_SendStatus(void);             // Now logs TX with counters
void CAN_PrintConfiguration(void);     // Print CAN ID configuration
```

## Integration

### Startup
The system automatically:
1. Detects MCP2515 presence
2. Initializes CAN bus at configured speed
3. Prints configuration (Board ID, CAN IDs)
4. Falls back to USART mode if MCP2515 not found

### Runtime
Every 100ms (main loop):
- Processes incoming CAN messages (with logging)
- Sends status messages (with periodic logging)
- Checks for errors

Every 10 seconds:
- Prints statistics summary

## Configuration

CAN speed is defined in `config.h`:
```c
#define CAN_SPEED MCP2515_SPEED_250KBPS
```

Options:
- `MCP2515_SPEED_125KBPS`
- `MCP2515_SPEED_250KBPS`
- `MCP2515_SPEED_500KBPS`
- `MCP2515_SPEED_1MBPS`

## Files Modified

1. **inc/can_bus.h** - Added new function declarations
2. **src/can_bus.c** - Enhanced with all debug features
3. **src/main.c** - Added statistics printout and error checking calls

## Usage Example

When operating in CAN controller mode, you'll see:

```
=== CAN Bus Initialization ===
Detecting MCP2515 module...
MCP2515 DETECTED!
Initializing CAN at 250 kbps...
CAN bus initialized successfully
CAN controller mode ACTIVE

=== CAN Configuration ===
Board ID: 0
Base CMD ID: 0x100
Base STATUS ID: 0x200

Command IDs:
  PWM:      0x100
  Speed:    0x101
  Position: 0x102
  Enable:   0x103

Status IDs:
  Speed:    0x200
  Position: 0x201
  Battery:  0x202
========================

[CAN RX] ID:0x101 DLC:8 Data: 64 00 00 00 64 00 00 00 [SPEED CMD] SPD1:100 SPD2:100 mm/s
[CAN TX] Speed: L=95 R=98 mm/s
[CAN TX] Position: L=1250 R=1265 mm
[CAN TX] Battery: 37200 mV, Temp: 28 C

...

=== CAN Statistics ===
RX Total:     45
  PWM:        15
  Speed:      20
  Position:   10
  Enable:     0
  Unknown:    0

TX Total:     300
  Speed:      100
  Position:   100
  Battery:    100

Errors:       0
======================
```

## Troubleshooting

If you see:
- **"MCP2515 NOT DETECTED"** - Check wiring, crystal oscillator (8MHz), CS/MOSI/MISO/SCK connections
- **"[CAN ERROR] Flags: 0xXX"** - CAN bus errors detected, check termination resistors (120Ω)
- **"[UNKNOWN]"** messages - Receiving CAN messages not for this board ID
- No RX messages - Check CAN_H/CAN_L connections, baud rate match

## Benefits

1. **Easy debugging** - See exactly what CAN messages are being sent/received
2. **Health monitoring** - Track packet counts and error rates
3. **Configuration verification** - Confirm MCP2515 detection and CAN IDs
4. **Message validation** - Decode command values to verify correct operation
5. **Error detection** - Catch bus errors, overflows, and communication issues early
