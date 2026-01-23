# CAN Controller Boot Debug Output Guide

## 🔍 Understanding Your Boot Messages

### What You're Seeing Now (Protocol-Wrapped Output)

```
W&Power button up at startup
W�W98&cf_speedCoef 10700, n_commDeacvHi 60, n_commAcvLo 30
```

**This is from the OLD protocol system.** The `W&` prefix is protocol framing.

---

## ✅ What You SHOULD See (Clean CAN Debug Output)

After uploading the new firmware, you should see:

```
=== CAN Bus Initialization ===
Detecting MCP2515 module...
MCP2515 DETECTED!
Initializing CAN at 500 kbps...
CAN bus initialized successfully
Enabling software serial on PA13/PA14...
CAN controller mode ACTIVE
NOTE: All debug messages will now appear automatically.
      No need to send 'E' or unlock commands.

Power button up at startup

==============================================
*** CAN CONTROLLER MODE ACTIVE ***
==============================================

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
========================

[CAN BOOT] Board ID:0 ONLINE - FW v1.0

[INFO] CAN bus ready - listening for messages...
[INFO] Statistics will print every 10 seconds
```

---

## 🤔 Why Are You Still Seeing Protocol Output?

### Possible Reasons:

### 1️⃣ **Old Firmware Still Running**
   - The board might have the old firmware
   - The new firmware was built but not uploaded yet
   - **Solution**: Upload the new firmware

### 2️⃣ **Upload Failed Silently**
   - ST-Link might not be connected
   - Wrong COM port selected
   - **Solution**: Verify ST-Link connection and retry upload

### 3️⃣ **MCP2515 Not Detected**
   - If MCP2515 fails detection, old messages might still appear
   - **Solution**: Check wiring, SPI connections, and power

### 4️⃣ **Multiple Serial Sources**
   - The protocol messages might be from a different UART
   - CAN messages use USART2 (raw output)
   - Old messages might be on Software Serial (protocol)
   - **Solution**: Make sure you're monitoring USART2 (115200 baud)

---

## 🔧 How to Fix This

### Step 1: Verify ST-Link Connection
```bash
python -m platformio device list
```
Should show your ST-Link device.

### Step 2: Upload New Firmware
```bash
# For Board 0
python -m platformio run -e control_CAN --target upload

# Or for other boards
python -m platformio run -e control_CAN_board1 --target upload
```

### Step 3: Monitor Serial Output
```bash
# Open serial monitor at 115200 baud
python -m platformio device monitor --baud 115200
```

### Step 4: Power Cycle the Board
- Disconnect power
- Wait 5 seconds
- Reconnect power
- Watch for clean boot messages

---

## 🧪 Testing the New Firmware

### 1. Check Boot Sequence
Power on and you should see:
1. CAN initialization messages (clean, no W&)
2. MCP2515 detection result
3. CAN configuration table
4. Boot announcement
5. "Ready for messages" indicator

### 2. Send CAN Message
Send a test command (e.g., PWM command) from another device:
```
CAN ID: 0x100
Data: [64 00 00 00 64 00 00 00]  // PWM1=100, PWM2=100
```

You should see:
```
[CAN RX] ID:0x100 DLC:8 Data: 64 00 00 00 64 00 00 00
  Decoded PWM Command: PWM1=100 PWM2=100
```

### 3. Verify Status Broadcasting
Every 100ms, the board should broadcast status:
```
[CAN TX] Status Speed - ID:0x200 DLC:8
[CAN TX] Status Position - ID:0x201 DLC:8
[CAN TX] Status Battery - ID:0x202 DLC:4
```

### 4. Check Periodic Statistics (Every 10 seconds)
```
=== CAN Statistics ===
Total RX: 1234
Total TX: 567
Commands: PWM=45 Speed=12 Position=0 Enable=0
Status Sent: Speed=100 Pos=100 Batt=100 Boot=1
Errors: 0
======================
```

---

## 🐛 Troubleshooting

### Still Seeing `W&` Protocol Output?

#### Check 1: Confirm Firmware Version
The new firmware is **58,440 bytes** (was 58,256 bytes before).

Check with:
```bash
ls -l .pio\build\control_CAN\firmware.bin
```

Should show: **58440 bytes** or **58.4 KB**

#### Check 2: Verify Flash Memory
Use STM32CubeProgrammer to read the firmware and check the size.

#### Check 3: Check UART Connections
- **USART2 TX** = PA2 (should connect to FTDI RX or ST-Link VCP)
- **USART2 RX** = PA3
- **Baud Rate** = 115200

#### Check 4: MCP2515 Detection
If MCP2515 is NOT detected, the firmware falls back to USART mode which might show protocol messages.

**To fix**: Verify MCP2515 wiring:
- **CS**: PB12
- **MOSI**: PB15
- **MISO**: PB14
- **SCK**: PB13
- **INT**: Not used (polling mode)
- **Power**: 3.3V or 5V (check your module)
- **GND**: Connected

---

## 📊 Output Comparison

### OLD Firmware (Protocol Wrapped):
```
W&Power button up at startup
W�W98&cf_speedCoef 10700, n_commDeacvHi 60, n_commAcvLo 30
h�1����W&power off by button
```

### NEW Firmware (Clean Output):
```
=== CAN Bus Initialization ===
Detecting MCP2515 module...
MCP2515 DETECTED!
Initializing CAN at 500 kbps...
CAN bus initialized successfully
Power button up at startup

==============================================
*** CAN CONTROLLER MODE ACTIVE ***
==============================================
```

---

## ⚡ Quick Upload Command

```bash
cd C:\RC\bipropellant
python -m platformio run -e control_CAN --target upload
```

Then power cycle and monitor:
```bash
python -m platformio device monitor --baud 115200 --filter direct
```

---

## 📝 Changes Made in New Firmware

1. ✅ Added 50ms delay after USART2 init (ensures UART is ready)
2. ✅ Added 100ms delay before post-boot CAN status (ensures stability)
3. ✅ Enhanced boot messages with clear headers
4. ✅ Added "Ready for messages" indicator
5. ✅ All CAN messages use `forceLog()` (raw UART, no protocol)
6. ✅ Firmware size: **58,440 bytes** (22.3% flash)

---

## 🎯 Expected Boot Timeline

```
Time    Event
----    -----
0ms     STM32 power on
50ms    USART2 initialized
100ms   CAN initialization starts
150ms   MCP2515 detection
200ms   CAN configured at 500 kbps
250ms   Flash content loaded
300ms   IDs calculated
350ms   Power button check
450ms   CAN status printed (clean output!)
550ms   Boot announcement sent on CAN bus
650ms   Ready for CAN messages
```

---

## 🚀 Final Checklist

- [ ] New firmware built (58,440 bytes)
- [ ] ST-Link connected
- [ ] Firmware uploaded successfully
- [ ] Board power cycled
- [ ] Serial monitor at 115200 baud
- [ ] Clean boot messages visible (no W&)
- [ ] MCP2515 detected message shown
- [ ] CAN configuration table displayed
- [ ] CAN RX/TX messages decoding correctly

If all checked ✅, you're ready to test! 🎉
