# Bipropellant Firmware Changelog

## CAN_BUS Board Type (Type 9)

### v1.14 (Current)
- **CRITICAL FIX**: Rewrote SoftSPI_Transfer() with correct SPI Mode 0 timing
- Previous bug: MISO was sampled 900ns AFTER rising clock edge (64 NOPs delay)
- Fix: Sample MISO immediately after rising edge (~56ns, 4 NOPs)
- Root cause: MCP2515 tV=45ns (data valid after falling edge), sampling too late caused drift
- Reduced effective SPI clock from ~50kHz to ~1.5MHz for tighter timing
- Removed diagnostic code from mcp2515.c and loopback test call
- Added -O3 compiler optimization attribute for consistent timing

### v1.7
- Removed internal pull-up on MISO pin (GPIO_NOPULL instead of GPIO_PULLUP)
- Pull-up was overpowering weak MCP2515 MISO output, causing all-1s reads
- Added SPI diagnostic: prints CNF1/CANSTAT when RxStatus=0xFF

### v1.6
- Rewrote SoftSPI_Transfer() using direct register access (BSRR/BRR/IDR)
- Much faster GPIO operations for more deterministic timing with interrupts
- Added CS setup delay (MCP2515 needs 100ns after CS low)
- Optimized delay structure: 32 NOPs for setup, 64 NOPs for MISO sampling

### v1.5
- Reverted interrupt disabling (was breaking software serial output)
- Added extra SPI delay before MISO sampling for more reliable reads
- Softer startup beep (freq 1→3→1, 50ms each)
- Softer power-off beep (freq 3→1, 80ms each)

### v1.4
- Added softer startup and power-off beeps
- IRQ protected SPI (caused garbled serial output - reverted in v1.5)

### v1.3
- Added interrupt protection to SPI transfers (`__disable_irq()`)
- Increased SPI delay from 32 to 64 NOPs
- Added SPI verification in statistics output (CNF register readback)

### v1.2
- Increased SPI delay from 32 to 64 NOPs
- Added version string to boot message: `FW v1.2 (SPI delay=64)`
- Added SPI health check in CAN statistics

### v1.1
- Fixed `SOFTWARE_SERIAL_BAUD` redefinition warning
- Added `#ifndef` guard around default baud rate

### v1.0
- Initial CAN_BUS board type implementation
- MCP2515 via Software SPI (PA2/PA3 MOSI/MISO, PB10/PB11 SCK/CS)
- Debug output via PA13/PA14 software serial (SWD pins)
- No hoverboard sensor support (pure CAN control)
- CAN message IDs:
  - Commands: 0x100 (PWM), 0x101 (Speed), 0x102 (Position), 0x103 (Enable)
  - Status: 0x200 (Speed), 0x201 (Position), 0x202 (Battery), 0x20F (Boot)

---

## Hardware Configuration

### Software SPI Pins (MCP2515)
| Signal | Pin | Port |
|--------|-----|------|
| MOSI   | PA2 | GPIOA |
| MISO   | PA3 | GPIOA |
| SCK    | PB10 | GPIOB |
| CS     | PB11 | GPIOB |
| INT    | PB12 | GPIOB |

### Debug Serial (Software Serial)
| Signal | Pin | Port |
|--------|-----|------|
| TX     | PA13 | GPIOA |
| RX     | PA14 | GPIOA |

Baud rate: 115200

---

## Known Issues

### CAN RX Reading All 1s (0xFFFFFFFF)
- **Symptom**: Hoverboard receives corrupt CAN messages (DLC=15, ID=0x0FFFFFFF)
- **Status**: Under investigation
- **Notes**:
  - ESP32 receives hoverboard TX correctly
  - Hoverboard TX works
  - Hoverboard RX reads all 1s
  - SPI init works (MCP2515 detected)
  - Loopback test on ESP32 passes
  - May be related to software SPI timing during runtime

---

## Build Commands

```bash
# Build CAN_BUS firmware
pio run -e CAN_BUS

# Build for specific board ID
pio run -e CAN_BUS_board1
```

## Flash Location
`C:\RC\bipropellant\.pio\build\CAN_BUS\firmware.bin`
