# CAN Board ID Configuration Guide

## Overview

The MCP2515 CAN implementation supports **Board IDs (0-15)** to allow multiple hoverboards on the same CAN bus. Each board responds to its own unique CAN IDs based on its configured Board ID.

---

## Setting the Board ID

### Method 1: Edit `config.h` (Permanent)

Open `inc/config.h` and modify the `CAN_BOARD_ID`:

```c
#if (CONTROL_TYPE == CAN_CONTROLLED)
  #define ENABLE_CAN_BUS 1
  #define CAN_SPEED MCP2515_SPEED_500KBPS
  #define CAN_BOARD_ID 0          // Change this: 0-15
  #define PHASE_ADV_ENA 0
  ...
#endif
```

**Valid values:** 0 to 15

### Method 2: PlatformIO Build Flag (Per-Build)

Add to `platformio.ini` in the `[env:control_CAN]` section:

```ini
[env:control_CAN]
build_flags =
    ...
    -D CONTROL_TYPE=7
    -D CAN_BOARD_ID=1        ; Board ID 1
```

### Method 3: Command Line (One-Time)

**PlatformIO:**
```bash
pio run -e control_CAN -D CAN_BOARD_ID=2
```

**Make:**
```bash
make CFLAGS="-DCAN_BOARD_ID=3"
```

### Method 4: Runtime Serial Configuration (Flash Storage)

**NEW:** Configure Board ID and Base IDs at runtime without recompiling!

#### Reading Current Configuration

Connect via serial (115200 baud) and read parameters:
```
rC0   // Read Board ID (0xC0)
rC1   // Read Command Base ID (0xC1)  
rC2   // Read Status Base ID (0xC2)
```

#### Writing Configuration

**Step 1:** Write desired values:
```
wC0 1        // Set Board ID to 1
wC1 256      // Set Command Base ID to 0x100 (decimal 256)
wC2 512      // Set Status Base ID to 0x200 (decimal 512)
```

**Step 2:** Commit changes to flash:
```
w80 1238     // Write magic number (1238) to save to flash
```

**Step 3:** Power cycle the board for changes to take effect.

#### Example: Configuring Board ID 2

```
wC0 2        // Set Board ID = 2
wC1 256      // Command Base = 0x100
wC2 512      // Status Base = 0x200
w80 1238     // Save to flash
```

After reboot, board will use:
- Command IDs: 0x120, 0x121, 0x122, 0x123
- Status IDs: 0x220, 0x221, 0x222

#### Display Configuration on Boot

When the board boots with CAN enabled, it will print:
```
=== CAN Configuration ===
Board ID: 1
Base CMD ID: 0x100
Base STATUS ID: 0x200

Command IDs:
  PWM:      0x110
  Speed:    0x111
  Position: 0x112
  Enable:   0x113

Status IDs:
  Speed:    0x210
  Position: 0x211
  Battery:  0x212
========================
```

#### Protocol Parameter IDs

| Parameter | ID | Type | Range | Description |
|-----------|-----|------|-------|-------------|
| CAN_BoardID | 0xC0 | SHORT | 0-15 | Board ID for CAN addressing |
| CAN_BaseID_CMD | 0xC1 | SHORT | 0-2047 | Base ID for command messages |
| CAN_BaseID_STATUS | 0xC2 | SHORT | 0-2047 | Base ID for status messages |
| Magic (save) | 0x80 | SHORT | 1238 | Write to commit changes |

---

## CAN ID Mapping

Each board ID gets its own range of CAN IDs:

### Formula
```
Command IDs = Base ID + (Board_ID × 0x10)
Status IDs  = Base ID + (Board_ID × 0x10)
```

### ID Tables

#### Board ID 0 (Default)
| Function | Type | CAN ID | Hex |
|----------|------|--------|-----|
| PWM Command | Command | 256 | 0x100 |
| Speed Command | Command | 257 | 0x101 |
| Position Command | Command | 258 | 0x102 |
| Enable Command | Command | 259 | 0x103 |
| Speed Status | Status | 512 | 0x200 |
| Position Status | Status | 513 | 0x201 |
| Battery Status | Status | 514 | 0x202 |

#### Board ID 1
| Function | Type | CAN ID | Hex |
|----------|------|--------|-----|
| PWM Command | Command | 272 | 0x110 |
| Speed Command | Command | 273 | 0x111 |
| Position Command | Command | 274 | 0x112 |
| Enable Command | Command | 275 | 0x113 |
| Speed Status | Status | 528 | 0x210 |
| Position Status | Status | 529 | 0x211 |
| Battery Status | Status | 530 | 0x212 |

#### Board ID 2
| Function | Type | CAN ID | Hex |
|----------|------|--------|-----|
| PWM Command | Command | 288 | 0x120 |
| Speed Command | Command | 289 | 0x121 |
| Position Command | Command | 290 | 0x122 |
| Enable Command | Command | 291 | 0x123 |
| Speed Status | Status | 544 | 0x220 |
| Position Status | Status | 545 | 0x221 |
| Battery Status | Status | 546 | 0x222 |

#### Full Table (Board ID 0-15)

| Board ID | PWM Cmd | Speed Cmd | Pos Cmd | Enable | Speed Status | Pos Status | Batt Status |
|----------|---------|-----------|---------|--------|--------------|------------|-------------|
| 0 | 0x100 | 0x101 | 0x102 | 0x103 | 0x200 | 0x201 | 0x202 |
| 1 | 0x110 | 0x111 | 0x112 | 0x113 | 0x210 | 0x211 | 0x212 |
| 2 | 0x120 | 0x121 | 0x122 | 0x123 | 0x220 | 0x221 | 0x222 |
| 3 | 0x130 | 0x131 | 0x132 | 0x133 | 0x230 | 0x231 | 0x232 |
| 4 | 0x140 | 0x141 | 0x142 | 0x143 | 0x240 | 0x241 | 0x242 |
| 5 | 0x150 | 0x151 | 0x152 | 0x153 | 0x250 | 0x251 | 0x252 |
| 6 | 0x160 | 0x161 | 0x162 | 0x163 | 0x260 | 0x261 | 0x262 |
| 7 | 0x170 | 0x171 | 0x172 | 0x173 | 0x270 | 0x271 | 0x272 |
| 8 | 0x180 | 0x181 | 0x182 | 0x183 | 0x280 | 0x281 | 0x282 |
| 9 | 0x190 | 0x191 | 0x192 | 0x193 | 0x290 | 0x291 | 0x292 |
| 10 | 0x1A0 | 0x1A1 | 0x1A2 | 0x1A3 | 0x2A0 | 0x2A1 | 0x2A2 |
| 11 | 0x1B0 | 0x1B1 | 0x1B2 | 0x1B3 | 0x2B0 | 0x2B1 | 0x2B2 |
| 12 | 0x1C0 | 0x1C1 | 0x1C2 | 0x1C3 | 0x2C0 | 0x2C1 | 0x2C2 |
| 13 | 0x1D0 | 0x1D1 | 0x1D2 | 0x1D3 | 0x2D0 | 0x2D1 | 0x2D2 |
| 14 | 0x1E0 | 0x1E1 | 0x1E2 | 0x1E3 | 0x2E0 | 0x2E1 | 0x2E2 |
| 15 | 0x1F0 | 0x1F1 | 0x1F2 | 0x1F3 | 0x2F0 | 0x2F1 | 0x2F2 |

---

## Multi-Board Setup Examples

### Example 1: Two Hoverboards

**Board 1 Config:**
```c
#define CAN_BOARD_ID 0  // Uses IDs 0x100-0x103, 0x200-0x202
```

**Board 2 Config:**
```c
#define CAN_BOARD_ID 1  // Uses IDs 0x110-0x113, 0x210-0x212
```

**Controller Code:**
```cpp
// Control Board 1
CAN_Frame frame;
frame.id = 0x100;  // Board 1 PWM
frame.dlc = 8;
// ... set data
CAN.sendMsgBuf(frame.id, 0, frame.dlc, frame.data);

// Control Board 2
frame.id = 0x110;  // Board 2 PWM
CAN.sendMsgBuf(frame.id, 0, frame.dlc, frame.data);
```

### Example 2: Four-Wheel Robot

**Front Left (Board ID 0):** Commands 0x100-0x103, Status 0x200-0x202  
**Front Right (Board ID 1):** Commands 0x110-0x113, Status 0x210-0x212  
**Rear Left (Board ID 2):** Commands 0x120-0x123, Status 0x220-0x222  
**Rear Right (Board ID 3):** Commands 0x130-0x133, Status 0x230-0x232

### Example 3: Broadcast to All Boards

To control all boards simultaneously, send to each board's ID:

```python
import can

bus = can.interface.Bus('can0', bustype='socketcan')

# Control all 4 boards
for board_id in range(4):
    pwm_id = 0x100 + (board_id * 0x10)
    msg = can.Message(arbitration_id=pwm_id, data=[...])
    bus.send(msg)
```

---

## Arduino Example with Board ID

```cpp
#include <mcp_can.h>

MCP_CAN CAN(10);

void controlBoard(uint8_t board_id, int32_t pwm1, int32_t pwm2) {
  // Calculate CAN ID for this board
  uint32_t can_id = 0x100 + (board_id * 0x10);
  
  uint8_t data[8];
  memcpy(&data[0], &pwm1, 4);
  memcpy(&data[4], &pwm2, 4);
  
  CAN.sendMsgBuf(can_id, 0, 8, data);
}

void setup() {
  CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  CAN.setMode(MCP_NORMAL);
}

void loop() {
  // Control board 0
  controlBoard(0, 300, 300);
  
  // Control board 1
  controlBoard(1, 400, 400);
  
  delay(20);
}
```

---

## Python Example with Board ID

```python
import can
import struct

bus = can.Bus(channel='can0', bustype='socketcan')

def send_pwm(board_id, pwm1, pwm2):
    """Send PWM command to specific board"""
    can_id = 0x100 + (board_id * 0x10)
    data = struct.pack('<ii', pwm1, pwm2)
    msg = can.Message(arbitration_id=can_id, data=data, is_extended_id=False)
    bus.send(msg)

def read_status(board_id):
    """Read status from specific board"""
    status_id = 0x200 + (board_id * 0x10)
    
    msg = bus.recv(timeout=1.0)
    if msg and msg.arbitration_id == status_id:
        speed1, speed2 = struct.unpack('<ii', msg.data)
        return speed1, speed2
    return None, None

# Control multiple boards
send_pwm(0, 300, 300)   # Board 0
send_pwm(1, -200, -200) # Board 1
send_pwm(2, 100, 100)   # Board 2

# Read status from board 1
speed1, speed2 = read_status(1)
print(f"Board 1 speeds: {speed1}, {speed2} mm/s")
```

---

## Best Practices

### 1. **Sequential Board IDs**
Use consecutive IDs (0, 1, 2, 3...) for easier management.

### 2. **Document Your Setup**
Keep a table of which board ID corresponds to which physical robot component.

### 3. **Physical Labeling**
Label each board with its ID (sticker on the hoverboard).

### 4. **Test One at a Time**
When setting up, configure and test each board individually before connecting all.

### 5. **Use Status Messages**
Monitor status IDs to verify which boards are responding:
```python
# Listen for all status messages
for msg in bus:
    board_id = (msg.arbitration_id - 0x200) // 0x10
    if 0 <= board_id <= 15:
        print(f"Board {board_id} is active")
```

---

## Troubleshooting

### Problem: Board not responding to commands

**Check:**
1. Board ID matches between firmware and controller
2. CAN ID calculation: `0x100 + (ID * 0x10)`
3. CAN bus termination (120Ω at both ends)
4. All boards using same CAN speed (500 kbps default)

### Problem: All boards respond to same command

**Cause:** All boards have same Board ID (0 is default)

**Solution:** Flash each board with unique ID

### Problem: Status messages collide

**Cause:** Multiple boards with same ID

**Solution:** Ensure unique IDs for each board

### Problem: Can't control board 10-15

**Cause:** IDs 10-15 require specific hex values

**Check:**
- Board 10: 0x1A0-0x1A3 (not 0x10A)
- Board 15: 0x1F0-0x1F3 (not 0x10F)

---

## Advanced: Dynamic Board ID

For advanced users, Board ID can be read from GPIO pins at boot:

```c
// In can_bus.c, modify CAN_Init_IDs():
uint8_t CAN_ReadBoardID_FromGPIO(void) {
    // Read 4 GPIO pins as board ID (DIP switches)
    uint8_t id = 0;
    id |= HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11) ? 0x01 : 0x00;
    id |= HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12) ? 0x02 : 0x00;
    id |= HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) ? 0x04 : 0x00;
    id |= HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_14) ? 0x08 : 0x00;
    return id & 0x0F;
}

// Then use:
uint8_t board_id = CAN_ReadBoardID_FromGPIO();
```

This allows setting Board ID with physical DIP switches without reflashing!

---

## Summary

- **Board IDs:** 0-15 supported
- **Configuration:** Set `CAN_BOARD_ID` in `config.h`
- **CAN ID Range:** Each board uses 7 unique IDs
- **Spacing:** 0x10 (16) between each board's ID range
- **Maximum Boards:** 16 boards on one CAN bus
- **Default ID:** 0 (if not specified)

---

**See also:**
- [CAN_BUS_README.md](CAN_BUS_README.md) - Complete CAN protocol
- [CAN_INTEGRATION_SUMMARY.md](CAN_INTEGRATION_SUMMARY.md) - Quick start
- [MCP2515_WIRING.md](MCP2515_WIRING.md) - Hardware setup
