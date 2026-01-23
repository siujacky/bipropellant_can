# Dual Board Setup Guide (Front + Rear)

## Overview

Configure two hoverboards on the same CAN bus for front/rear motor control. This matches the controller configuration:
- **Front Board**: CAN Cmd ID = 0x100, Feedback ID = 0x200
- **Rear Board**: CAN Cmd ID = 0x110, Feedback ID = 0x210

## Quick Setup

### Front Hoverboard (Board ID 0)

Connect via serial (115200 baud) and configure:

```
wC0 0        // Board ID = 0 (Front)
wC1 256      // Command Base = 0x100 (256 decimal)
wC2 512      // Status Base = 0x200 (512 decimal)
w80 1238     // Save to flash
```

**Power cycle.** Front board will use:
- Command IDs: 0x100 (PWM), 0x101 (Speed), 0x102 (Position), 0x103 (Enable)
- Status IDs: 0x200 (Speed), 0x201 (Position), 0x202 (Battery/Temp)

### Rear Hoverboard (Board ID 1)

Connect via serial (115200 baud) and configure:

```
wC0 1        // Board ID = 1 (Rear)
wC1 256      // Command Base = 0x100 (256 decimal)
wC2 512      // Status Base = 0x200 (512 decimal)
w80 1238     // Save to flash
```

**Power cycle.** Rear board will use:
- Command IDs: 0x110 (PWM), 0x111 (Speed), 0x112 (Position), 0x113 (Enable)
- Status IDs: 0x210 (Speed), 0x211 (Position), 0x212 (Battery/Temp)

## ID Calculation Explained

```
Board ID 0: Base + (0 × 0x10) = Base + 0x00
Board ID 1: Base + (1 × 0x10) = Base + 0x10
```

So with Command Base = 0x100:
- Front PWM: 0x100 + 0x00 = **0x100**
- Rear PWM: 0x100 + 0x10 = **0x110** ✓

With Status Base = 0x200:
- Front Feedback: 0x200 + 0x00 = **0x200**
- Rear Feedback: 0x200 + 0x10 = **0x210** ✓

## CAN Message Format

### PWM Command (0x100 Front, 0x110 Rear)
**8 bytes, little-endian int32:**

```
Byte 0-3: Left motor PWM  (int32, -1000 to 1000)
Byte 4-7: Right motor PWM (int32, -1000 to 1000)
```

**Example:** Send PWM=500 to both motors
```
CAN ID: 0x100 (Front) or 0x110 (Rear)
Data: F4 01 00 00 F4 01 00 00
      └─ 500 ─┘   └─ 500 ─┘
```

### Speed Status (0x200 Front, 0x210 Rear)
**8 bytes:**

```
Byte 0-3: Left motor speed  (int32, RPM)
Byte 4-7: Right motor speed (int32, RPM)
```

### Position Status (0x201 Front, 0x211 Rear)
**8 bytes:**

```
Byte 0-3: Left motor position  (int32, encoder counts)
Byte 4-7: Right motor position (int32, encoder counts)
```

### Battery Status (0x202 Front, 0x212 Rear)
**4 bytes:**

```
Byte 0-1: Battery voltage (int16, volts × 100)
Byte 2-3: Board temperature (int16, °C)
```

## Wiring

### CAN Bus Topology

```
Controller ──┬── MCP2515 ── CAN_H ──┬── 120Ω ── Front Hoverboard MCP2515
             │                      │
             └── (Optional) ────────┴── 120Ω ── Rear Hoverboard MCP2515
                                    │
                                  CAN_L
```

**Requirements:**
- 120Ω termination resistors at **both ends** of CAN bus
- Twisted pair cable for CAN_H and CAN_L
- All grounds connected
- Both MCP2515 modules configured for same speed (default: 500kbps)

### Per-Board Connections

Each hoverboard connects to MCP2515 via software SPI:

| STM32F103RC Pin | MCP2515 Pin | Function |
|-----------------|-------------|----------|
| PA2 | SI (MOSI) | SPI Data Out |
| PA3 | SO (MISO) | SPI Data In |
| PB10 | SCK | SPI Clock |
| PB11 | CS | Chip Select |
| PB12 | INT | Interrupt (optional) |
| 3.3V | VCC | Power |
| GND | GND | Ground |

**Note:** PA2, PA3, PB10, PB11 are shared with USART2/USART3. Boards auto-detect MCP2515 on boot and fall back to USART if not found.

## Verification

### Check Configuration on Boot

Connect via serial console. After power-on, you should see:

**Front Board:**
```
CAN bus detected and initialized
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
```

**Rear Board:**
```
CAN bus detected and initialized
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

### Test with CAN Analyzer

1. **Send PWM=300 to Front Board:**
   ```
   CAN ID: 0x100
   Data: 2C 01 00 00 2C 01 00 00
   ```

2. **Send PWM=300 to Rear Board:**
   ```
   CAN ID: 0x110
   Data: 2C 01 00 00 2C 01 00 00
   ```

3. **Monitor Status Messages:**
   - Front: 0x200, 0x201, 0x202 (every 100ms)
   - Rear: 0x210, 0x211, 0x212 (every 100ms)

## Controller Integration

Your controller should:

1. **Send Commands:**
   - Front PWM → CAN ID 0x100
   - Rear PWM → CAN ID 0x110

2. **Read Feedback:**
   - Front Status ← CAN ID 0x200, 0x201, 0x202
   - Rear Status ← CAN ID 0x210, 0x211, 0x212

3. **Update Rate:**
   - Command: As needed (20-100 Hz typical)
   - Status: Automatically sent every 100ms by hoverboards

## Mixed Mode Operation

You can run:
- **Both on CAN**: Configure both boards with MCP2515 modules
- **Both on USART**: Don't connect MCP2515 modules, use USART2/3
- **Front on CAN, Rear on USART**: Only front board has MCP2515
- **Front on USART, Rear on CAN**: Only rear board has MCP2515

Each board auto-detects its communication mode on boot!

## Troubleshooting

### Board doesn't respond to CAN commands

1. Check Board ID matches controller expectation:
   ```
   rC0   // Should return 0 (front) or 1 (rear)
   ```

2. Check Base IDs:
   ```
   rC1   // Should return 256 (0x100)
   rC2   // Should return 512 (0x200)
   ```

3. Verify CAN speed matches (default 500kbps)

4. Check termination resistors (120Ω at both ends)

5. Look for boot message showing correct IDs

### Status messages not received

1. Check CAN bus wiring (CAN_H, CAN_L, GND)
2. Verify controller is listening for correct IDs (0x200/0x210)
3. Use CAN analyzer to confirm messages are being sent
4. Check for bus-off errors (disconnect/reconnect power)

### One board works, other doesn't

1. Verify each board has unique Board ID (0 vs 1)
2. Check both boards show "CAN bus detected" on boot
3. Verify both MCP2515 modules powered and wired correctly
4. Test each board individually on CAN bus

## Advanced: Custom Base IDs

If your controller uses different base IDs (e.g., 0x300/0x400):

**Front Board:**
```
wC0 0        // Board ID = 0
wC1 768      // Command Base = 0x300 (768 decimal)
wC2 1024     // Status Base = 0x400 (1024 decimal)
w80 1238     // Save
```

**Rear Board:**
```
wC0 1        // Board ID = 1
wC1 768      // Command Base = 0x300
wC2 1024     // Status Base = 0x400
w80 1238     // Save
```

Result:
- Front: Cmd=0x300, Status=0x400
- Rear: Cmd=0x310, Status=0x410

## Summary Table

| Board | Board ID | Cmd Base | Status Base | PWM ID | Feedback IDs |
|-------|----------|----------|-------------|--------|--------------|
| Front | 0 | 0x100 | 0x200 | 0x100 | 0x200-0x202 |
| Rear | 1 | 0x100 | 0x200 | 0x110 | 0x210-0x212 |

**Serial Configuration:** Just 4 commands per board + power cycle = Done! ✓
