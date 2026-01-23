# Lessons Learned: STM32F103 + MCP2515 Software SPI

## Project Summary
- **MCU**: STM32F103RC @ 72MHz
- **CAN Controller**: MCP2515 with 8MHz crystal
- **SPI**: Software (bit-banged) SPI on GPIO pins
- **Versions tested**: 24 firmware iterations to solve the issue

## The Problem
CAN RX was reading corrupt data (`0x7F FF FF FF FF FF FF FF FF`) while:
- CAN TX worked correctly
- Single register reads worked (CNF1=0x00 correct)
- Multi-byte RX buffer reads failed

## Root Cause
**SPI MISO sampling timing was wrong.** The MISO line was being sampled too early after the rising clock edge, before the signal had stabilized.

---

## Key Lessons

### 1. Sample MISO Late in Clock High Period

**Wrong approach (v1.14-v1.22):**
```c
// Rising edge
SOFT_SPI_SCK_PORT->BSRR = SOFT_SPI_SCK_PIN;

// Sample too early (4 NOPs = ~56ns)
__NOP(); __NOP(); __NOP(); __NOP();
if (SOFT_SPI_MISO_PORT->IDR & SOFT_SPI_MISO_PIN) { ... }
```

**Correct approach (v1.24):**
```c
// Rising edge
SOFT_SPI_SCK_PORT->BSRR = SOFT_SPI_SCK_PIN;

// Wait longer for signal to stabilize (16 NOPs = ~224ns)
__NOP(); __NOP(); __NOP(); __NOP();
__NOP(); __NOP(); __NOP(); __NOP();
__NOP(); __NOP(); __NOP(); __NOP();
__NOP(); __NOP(); __NOP(); __NOP();

// Sample MISO late, just before falling edge
if (SOFT_SPI_MISO_PORT->IDR & SOFT_SPI_MISO_PIN) { ... }
```

### 2. MISO Pull Configuration

| Configuration | Result |
|--------------|--------|
| `GPIO_PULLUP` | All 1s - pull-up overpowered MCP2515's weak MISO drive |
| `GPIO_PULLDOWN` | MCP2515 not detected - pull-down too strong |
| `GPIO_NOPULL` | Works with proper timing |

**Lesson:** MCP2515 has weak MISO output drive. Internal pull resistors (~40kΩ) can overpower it.

### 3. Symptom Pattern Analysis

The pattern `0x7F FF FF FF...` told us:
- `0x7F` = `0111 1111` - first bit (MSB) correct, rest wrong
- First 4 bits often correct, last 4 bits wrong
- **Diagnosis:** Timing drift within each byte, getting worse over time

Later pattern `CNF2=0x9F` instead of `0x90`:
- Upper nibble correct (1001), lower nibble wrong (1111 vs 0000)
- **Diagnosis:** Lower bits (sampled later) drifting high

### 4. Debug Output Location Matters

**Problem:** Diagnostic messages in `MCP2515_Init()` never appeared.

**Cause:** Software serial was initialized AFTER `MCP2515_Init()` completed.

**Solution:** Add diagnostics AFTER serial initialization:
```c
SoftwareSerialInit();  // Initialize serial first
forceLog("PA13 test blink complete\r\n");

// NOW run SPI diagnostics
uint8_t cnf1 = MCP2515_ReadRegister(MCP2515_CNF1);
// ... diagnostic output will appear
```

### 5. Single vs Multi-Byte Read Behavior

| Read Type | Bytes | Result in broken code |
|-----------|-------|----------------------|
| Single register | 3 SPI transfers | Often worked |
| Multi-byte | 14+ SPI transfers | Failed after first few bytes |

**Why:** Timing issues accumulated over multiple transfers. Single reads completed before drift became critical.

### 6. CAN "No ACK" = Physical Layer Issue

ESP32 showing:
```
[CAN] TX error: ALLTXBUSY (id=0x300)
[CAN] TX buffers cleared (no ACK - check if other node is running)
```

**Means:** The MCP2515 wasn't ACKing messages because:
1. Not in Normal mode, OR
2. Wrong bitrate, OR
3. SPI communication broken (couldn't configure properly)

### 7. MCP2515 Mode Verification

Always verify CANSTAT after setting mode:
```c
uint8_t canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
// Bits 7:5 = OPMOD
// 000 = Normal, 001 = Sleep, 010 = Loopback, 011 = Listen-Only, 100 = Config
```

If `CANSTAT` shows wrong mode, the MCP2515 won't ACK CAN messages.

---

## Working SPI Transfer Function (v1.24)

```c
__attribute__((optimize("-O3")))
uint8_t SoftSPI_Transfer(uint8_t data) {
    uint8_t received = 0;

    for (int i = 7; i >= 0; i--) {
        // 1. Set MOSI bit
        if (data & (1 << i)) {
            SOFT_SPI_MOSI_PORT->BSRR = SOFT_SPI_MOSI_PIN;
        } else {
            SOFT_SPI_MOSI_PORT->BRR = SOFT_SPI_MOSI_PIN;
        }

        // 2. MOSI setup time (~110ns)
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();

        // 3. Rising edge
        SOFT_SPI_SCK_PORT->BSRR = SOFT_SPI_SCK_PIN;

        // 4. Wait for signal to stabilize (~224ns)
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();

        // 5. Sample MISO (late in high period)
        if (SOFT_SPI_MISO_PORT->IDR & SOFT_SPI_MISO_PIN) {
            received |= (1 << i);
        }

        // 6. Falling edge
        SOFT_SPI_SCK_PORT->BRR = SOFT_SPI_SCK_PIN;

        // 7. Wait for MCP2515 to output next bit (~224ns)
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
    }

    return received;
}
```

---

## MCP2515 SPI Timing Requirements

From datasheet (VDD = 5V):

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| Clock frequency | fSCK | - | 10 | MHz |
| Clock high time | tCH | 50 | - | ns |
| Clock low time | tCL | 50 | - | ns |
| CS setup time | tCSS | 100 | - | ns |
| Data valid after SCK falling | tV | - | 45 | ns |
| Data setup before SCK rising | tSU | 10 | - | ns |

**Key insight:** `tV = 45ns max` means data is valid very quickly after falling edge. But on a noisy/long wire setup, give MORE margin.

---

## Hardware Recommendations

1. **Keep SPI wires short** - Long wires increase capacitance and noise
2. **Add 100nF capacitor** close to MCP2515 VDD pin
3. **Consider external pull-down** (~10kΩ) on MISO if floating issues persist
4. **Use hardware SPI** if available - much more reliable than bit-banging
5. **Check crystal frequency** - Wrong crystal = wrong bitrate = no communication

---

## Debugging Checklist

1. **SPI working?**
   - Write 0xAA to CNF1, read back - should be 0xAA
   - Write 0x55 to CNF1, read back - should be 0x55

2. **Multi-byte working?**
   - Read CNF1, CNF2, CNF3 individually
   - Read CNF3, CNF2, CNF1 as multi-byte
   - Compare results

3. **Mode correct?**
   - Read CANSTAT, check bits 7:5 = 000 for Normal mode

4. **CAN bus connected?**
   - TX should work even without RX
   - If "no ACK", receiver isn't seeing valid frames

5. **Bitrate matched?**
   - Both ends must have identical bitrate settings
   - Check CNF1, CNF2, CNF3 values

---

## Version History Summary

| Version | Change | Result |
|---------|--------|--------|
| v1.0-v1.6 | Various timing tweaks | Still corrupt |
| v1.7 | Removed MISO pull-up | Pattern changed to 0x7F FF... |
| v1.14 | Faster timing (4 NOPs) | Still corrupt |
| v1.22 | Added boot diagnostic | Saw CNF2=0x9F (lower bits wrong) |
| v1.23 | MISO pull-down | No output (too strong) |
| v1.24 | Sample late (16 NOPs), NOPULL | **WORKING!** |

---

## Final Working Configuration

```
MCU: STM32F103RC @ 72MHz
MCP2515: 8MHz crystal, 500kbps CAN

GPIO:
- PA2: MOSI (Output, Push-Pull, No Pull)
- PA3: MISO (Input, No Pull)
- PB10: SCK (Output, Push-Pull, No Pull)
- PB11: CS (Output, Push-Pull, No Pull)

SPI Timing:
- MOSI setup: 8 NOPs (~112ns)
- Clock high with late sample: 16 NOPs (~224ns)
- Clock low: 16 NOPs (~224ns)
- Effective SPI clock: ~300kHz
```

---

*Document created after 24 firmware versions and extensive debugging. The key lesson: when bit-banging SPI, sample MISO LATE in the clock high period, not immediately after the rising edge.*
