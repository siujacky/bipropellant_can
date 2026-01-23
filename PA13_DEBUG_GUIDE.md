# PA13/PA14 Software Serial Debug Guide

## 🎯 YOUR SETUP (Correct!)

Since **PA2, PA3, PB10, PB11** are used for MCP2515 SPI, you must use **PA13/PA14** for serial debug.

## 📌 Pin Connections

```
STM32 Board          FTDI/USB Adapter
-----------          ----------------
PA13 (TX)    ----->  RX
PA14 (RX)    <-----  TX
GND          <---->  GND
```

**Baud Rate**: 115200

## 🔬 New Test Firmware (v4)

The latest firmware (**58,624 bytes**) includes a **PA13 blink test** to verify the pin is working!

### What Happens on Boot:

1. MCP2515 detected
2. "Enabling software serial on PA13/PA14..." printed
3. **PA13 blinks 10 times** (can see on oscilloscope or LED)
4. "PA13 test blink complete" printed
5. Normal CAN debug output starts

### How to Test:

#### Test 1: Visual Verification (with LED)
```
Connect LED + resistor between PA13 and GND:
PA13 ----[330Ω]----[LED]----GND
```
After MCP2515 detection, LED should **blink 10 times rapidly** (100ms cycle).

#### Test 2: Oscilloscope/Logic Analyzer
- Connect probe to PA13
- Should see 10 square wave pulses (0V to 3.3V)
- Each pulse ~10ms period

#### Test 3: Serial Output
- Connect FTDI RX to PA13
- Set terminal to 115200 baud
- Should see CAN debug messages

## 📊 Expected Serial Output on PA13

```
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
...
```

## 🐛 Troubleshooting

### Still No Output?

#### Step 1: Check MCP2515 Wiring
Software Serial is ONLY enabled if MCP2515 is detected.

**MCP2515 SPI Connections**:
```
STM32     MCP2515
-----     -------
PA2   --> SI (MOSI)
PA3   <-- SO (MISO)
PB10  --> SCK
PB11  --> CS
3.3V  --> VCC
GND   --> GND
```

**Verify**: You should see MCP2515 initialization messages (check with ST-Link SWO or before PA13 init).

#### Step 2: Check PA13 is Actually Toggling
1. Connect LED to PA13 (see Test 1 above)
2. Power on board
3. If MCP2515 detected, LED should blink 10 times
4. If LED doesn't blink → PA13 stuck or not initialized

#### Step 3: Verify JTAG/SWD Disabled
PA13/PA14 are SWDIO/SWCLK pins. Code disables JTAG/SWD:
```c
__HAL_AFIO_REMAP_SWJ_DISABLE();  // In SoftwareSerialInit()
```

**Important**: After flashing, **disconnect ST-Link** completely!
- ST-Link may keep JTAG enabled even after code disables it
- Power cycle WITHOUT ST-Link connected

#### Step 4: Check TIM3 Interrupt
Software Serial TX uses TIM3 timer interrupt.

**Verify TIM3 is not used elsewhere**:
```bash
# Search for other TIM3 usage
grep -r "TIM3" src/
```

If TIM3 is used by another module → conflict!

#### Step 5: Software Serial Buffer
Software Serial uses interrupt-driven TX buffer.

**Check buffer isn't full**:
- Buffer size: 1024 bytes
- If buffer fills → overflow counter increments
- Add debug: Check `softwareserialTXbuffer.overflow`

## 🔧 Alternative: Force Direct TX (Bypass Buffering)

If Software Serial buffer isn't working, modify `forceLog()` to use direct bit-bang:

### Simple Bit-Bang TX Function:
```c
void bitbang_putchar(char c) {
    // Start bit
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, 0);
    for(volatile int i = 0; i < 625; i++);  // ~8.68us @ 72MHz = 115200 baud
    
    // 8 data bits (LSB first)
    for(int bit = 0; bit < 8; bit++) {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, (c >> bit) & 1);
        for(volatile int i = 0; i < 625; i++);
    }
    
    // Stop bit
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_13, 1);
    for(volatile int i = 0; i < 625; i++);
}
```

This bypasses Software Serial completely - direct GPIO toggling.

## 💡 Quick Diagnostic Commands

### Upload Test Firmware:
```bash
python -m platformio run -e control_CAN --target upload
```

### Expected Firmware Size:
- **v4**: 58,624 bytes (with PA13 blink test)
- **v3**: 58,504 bytes (without blink test)

### Serial Monitor:
```bash
# If you have another serial port for debug
python -m platformio device monitor --baud 115200 --port COM_X
```

## 📋 Summary

**Your setup is correct**: PA13=TX, PA14=RX

**To verify PA13 works**:
1. Upload v4 firmware (58,624 bytes)
2. Power cycle WITHOUT ST-Link
3. Connect LED to PA13
4. Should see 10 blinks after MCP2515 detected

**If LED blinks**: PA13 GPIO works → Problem is serial buffering/timing
**If LED doesn't blink**: PA13 stuck → JTAG still enabled or wiring issue

---

**Next step**: Upload firmware and tell me:
- Do you see PA13 blink (with LED or scope)?
- Do you see any output on serial monitor?
