# CAN Multi-Board Firmware Guide

## 📦 Pre-Built Firmware for 5 Different Boards

All firmware versions are ready to upload! Each board has its own unique CAN IDs.

---

## 🎯 Board ID 0 (Default)

**Firmware**: `.pio\build\control_CAN\firmware.bin`  
**Size**: 58,256 bytes (22.2% flash)

### Configuration:
- **Board ID**: 0
- **CAN Speed**: 500 kbps
- **Command IDs**:
  - PWM: `0x100`
  - Speed: `0x101`
  - Position: `0x102`
  - Enable: `0x103`
- **Status IDs**:
  - Speed: `0x200`
  - Position: `0x201`
  - Battery: `0x202`
  - Boot: `0x20F`

### Upload:
```bash
python -m platformio run -e control_CAN --target upload
```

---

## 🎯 Board ID 1

**Firmware**: `.pio\build\control_CAN_board1\firmware.bin`  
**Size**: 58,256 bytes

### Configuration:
- **Board ID**: 1
- **CAN Speed**: 500 kbps
- **Command IDs**:
  - PWM: `0x110` (0x100 + 0x10 offset)
  - Speed: `0x111`
  - Position: `0x112`
  - Enable: `0x113`
- **Status IDs**:
  - Speed: `0x210` (0x200 + 0x10 offset)
  - Position: `0x211`
  - Battery: `0x212`
  - Boot: `0x21F`

### Upload:
```bash
python -m platformio run -e control_CAN_board1 --target upload
```

---

## 🎯 Board ID 2

**Firmware**: `.pio\build\control_CAN_board2\firmware.bin`  
**Size**: 58,256 bytes

### Configuration:
- **Board ID**: 2
- **CAN Speed**: 500 kbps
- **Command IDs**:
  - PWM: `0x120` (0x100 + 0x20 offset)
  - Speed: `0x121`
  - Position: `0x122`
  - Enable: `0x123`
- **Status IDs**:
  - Speed: `0x220` (0x200 + 0x20 offset)
  - Position: `0x221`
  - Battery: `0x222`
  - Boot: `0x22F`

### Upload:
```bash
python -m platformio run -e control_CAN_board2 --target upload
```

---

## 🎯 Board ID 3

**Firmware**: `.pio\build\control_CAN_board3\firmware.bin`  
**Size**: 58,256 bytes

### Configuration:
- **Board ID**: 3
- **CAN Speed**: 500 kbps
- **Command IDs**:
  - PWM: `0x130` (0x100 + 0x30 offset)
  - Speed: `0x131`
  - Position: `0x132`
  - Enable: `0x133`
- **Status IDs**:
  - Speed: `0x230` (0x200 + 0x30 offset)
  - Position: `0x231`
  - Battery: `0x232`
  - Boot: `0x23F`

### Upload:
```bash
python -m platformio run -e control_CAN_board3 --target upload
```

---

## 🎯 Board ID 4

**Firmware**: `.pio\build\control_CAN_board4\firmware.bin`  
**Size**: 58,256 bytes

### Configuration:
- **Board ID**: 4
- **CAN Speed**: 500 kbps
- **Command IDs**:
  - PWM: `0x140` (0x100 + 0x40 offset)
  - Speed: `0x141`
  - Position: `0x142`
  - Enable: `0x143`
- **Status IDs**:
  - Speed: `0x240` (0x200 + 0x40 offset)
  - Position: `0x241`
  - Battery: `0x242`
  - Boot: `0x24F`

### Upload:
```bash
python -m platformio run -e control_CAN_board4 --target upload
```

---

## 📋 CAN ID Offset System

Each board ID gets a **0x10 offset** for its CAN IDs:
- **Board 0**: Base IDs (0x100, 0x200)
- **Board 1**: Base + 0x10 (0x110, 0x210)
- **Board 2**: Base + 0x20 (0x120, 0x220)
- **Board 3**: Base + 0x30 (0x130, 0x230)
- **Board 4**: Base + 0x40 (0x140, 0x240)

This allows up to **16 boards** (0-15) on the same CAN bus without ID conflicts!

---

## 🧪 Testing Multiple Boards on Same CAN Bus

### Example: 3 Boards on One Bus

1. **Flash Board 0** with `control_CAN`
2. **Flash Board 1** with `control_CAN_board1`
3. **Flash Board 2** with `control_CAN_board2`
4. **Connect all to same CAN bus** (CAN_H, CAN_L, GND)
5. **Add 120Ω termination** resistors at each end of bus

### Send Commands:

**Control Board 0 PWM:**
```
CAN ID: 0x100
Data: [64 00 00 00 C8 00 00 00]  // PWM1=100, PWM2=200
```

**Control Board 1 Speed:**
```
CAN ID: 0x111
Data: [E8 03 00 00 D0 07 00 00]  // Speed1=1000, Speed2=2000 mm/s
```

**Control Board 2 Position:**
```
CAN ID: 0x122
Data: [10 27 00 00 20 4E 00 00]  // Pos1=10000, Pos2=20000 mm
```

### Monitor Boot Messages:

When all boards power on, you'll see:
```
[CAN BOOT] Board ID:0 ONLINE - FW v1.0  (on ID 0x20F)
[CAN BOOT] Board ID:1 ONLINE - FW v1.0  (on ID 0x21F)
[CAN BOOT] Board ID:2 ONLINE - FW v1.0  (on ID 0x22F)
```

---

## 🔍 Serial Console Output

Each board will display its configuration on boot:

### Board 0:
```
=== CAN Configuration ===
Board ID: 0
CAN Speed: 500 kbps
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
  Boot:     0x20F
```

### Board 1:
```
=== CAN Configuration ===
Board ID: 1
CAN Speed: 500 kbps
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
  Boot:     0x21F
```

---

## ⚡ Quick Upload Commands

```bash
# Board 0
python -m platformio run -e control_CAN --target upload

# Board 1
python -m platformio run -e control_CAN_board1 --target upload

# Board 2
python -m platformio run -e control_CAN_board2 --target upload

# Board 3
python -m platformio run -e control_CAN_board3 --target upload

# Board 4
python -m platformio run -e control_CAN_board4 --target upload
```

---

## 🛠️ Manual Flash with STM32CubeProgrammer

1. Open STM32CubeProgrammer
2. Connect via ST-Link
3. Select firmware file:
   - Board 0: `.pio\build\control_CAN\firmware.bin`
   - Board 1: `.pio\build\control_CAN_board1\firmware.bin`
   - Board 2: `.pio\build\control_CAN_board2\firmware.bin`
   - Board 3: `.pio\build\control_CAN_board3\firmware.bin`
   - Board 4: `.pio\build\control_CAN_board4\firmware.bin`
4. Set start address: `0x08000000`
5. Click "Download"

---

## 📊 CAN ID Reference Table

| Board | PWM   | Speed | Position | Enable | Speed Status | Pos Status | Battery | Boot  |
|-------|-------|-------|----------|--------|--------------|------------|---------|-------|
| 0     | 0x100 | 0x101 | 0x102    | 0x103  | 0x200        | 0x201      | 0x202   | 0x20F |
| 1     | 0x110 | 0x111 | 0x112    | 0x113  | 0x210        | 0x211      | 0x212   | 0x21F |
| 2     | 0x120 | 0x121 | 0x122    | 0x123  | 0x220        | 0x221      | 0x222   | 0x22F |
| 3     | 0x130 | 0x131 | 0x132    | 0x133  | 0x230        | 0x231      | 0x232   | 0x23F |
| 4     | 0x140 | 0x141 | 0x142    | 0x143  | 0x240        | 0x241      | 0x242   | 0x24F |

---

## ✅ Features

- ✅ Auto CAN speed detection (500 kbps)
- ✅ Boot announcement on CAN bus
- ✅ Real-time RX/TX message logging
- ✅ Decoded command display
- ✅ Error monitoring (RXWAR, TXEP, etc.)
- ✅ Statistics every 10 seconds
- ✅ Clean UART output (no protocol framing)
- ✅ Individual board IDs for multi-board setup
- ✅ Unique CAN IDs per board (no conflicts)

---

## 🚀 Ready to Test!

All 5 firmware versions are pre-built and ready to flash!
Just connect ST-Link, pick the board ID you want, and upload! 🎉
