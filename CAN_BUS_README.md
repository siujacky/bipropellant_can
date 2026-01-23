# MCP2515 CAN Bus Integration

This firmware now supports MCP2515 CAN bus communication with automatic detection and fallback to USART2/USART3.

## Hardware Connections

### MCP2515 Module Wiring

| MCP2515 Pin | STM32F103RC Pin | Function | Notes |
|-------------|-----------------|----------|-------|
| **VCC** | 5V or 3.3V | Power | MCP2515 supports both voltages |
| **GND** | GND | Ground | Common ground |
| **MOSI** | PA2 | SPI Data Out | Shared with USART2_TX |
| **MISO** | PA3 | SPI Data In | Shared with USART2_RX |
| **SCK** | PB10 | SPI Clock | Shared with USART3_TX |
| **CS** | PB11 | Chip Select | Shared with USART3_RX |
| **INT** | PB12 | Interrupt | Optional, free pin |

**Important:** The MCP2515 shares pins with USART2 and USART3. The firmware automatically detects which mode to use on boot.

### MCP2515 Module Requirements

- **Crystal:** 8 MHz (default configuration)
- **CAN Transceiver:** TJA1050, MCP2551, or compatible
- **Termination:** 120Ω resistor if at end of CAN bus

## Auto-Detection

On startup, the firmware:
1. Attempts to detect MCP2515 via SPI communication
2. If detected: Initializes CAN bus mode
3. If not detected: Falls back to USART2/USART3 mode

This allows a **single firmware** to work with:
- MCP2515 CAN bus module
- Original USART2/USART3 sensor boards
- No hardware changes needed - plug and play!

## Building

### Using PlatformIO

Build the CAN control environment:

```bash
pio run -e control_CAN
```

Upload to board:

```bash
pio run -e control_CAN --target upload
```

### Configuration

Edit `platformio.ini` to change the default environment:

```ini
[platformio]
default_envs = control_CAN
```

Or set in `inc/config.h`:

```c
#define CONTROL_TYPE CAN_CONTROLLED
```

## CAN Protocol

### CAN Bus Speed

Default: **500 kbps** (configurable in `config.h`)

Available speeds:
- 125 kbps: `MCP2515_SPEED_125KBPS`
- 250 kbps: `MCP2515_SPEED_250KBPS`
- 500 kbps: `MCP2515_SPEED_500KBPS`
- 1 Mbps: `MCP2515_SPEED_1MBPS`

### Command Messages (Controller → Hoverboard)

#### 0x100: PWM Command
Send direct PWM values to motors.

| Byte | Data Type | Description |
|------|-----------|-------------|
| 0-3 | int32_t | PWM Motor 1 (-1000 to 1000) |
| 4-7 | int32_t | PWM Motor 2 (-1000 to 1000) |

**Example:**
```
CAN ID: 0x100
DLC: 8
Data: [2C 01 00 00 D4 FE FF FF]  // PWM1=300, PWM2=-300
```

#### 0x101: Speed Command
Send speed setpoints in mm/s.

| Byte | Data Type | Description |
|------|-----------|-------------|
| 0-3 | int32_t | Speed Motor 1 (mm/s) |
| 4-7 | int32_t | Speed Motor 2 (mm/s) |

#### 0x102: Position Command
Send position setpoints in mm.

| Byte | Data Type | Description |
|------|-----------|-------------|
| 0-3 | int32_t | Position Motor 1 (mm) |
| 4-7 | int32_t | Position Motor 2 (mm) |

#### 0x103: Enable/Disable
Enable or disable motor output.

| Byte | Data Type | Description |
|------|-----------|-------------|
| 0 | uint8_t | 0=Disable, 1=Enable |

### Status Messages (Hoverboard → Controller)

Sent automatically every 100ms when in CAN mode.

#### 0x200: Speed Feedback
Current motor speeds.

| Byte | Data Type | Description |
|------|-----------|-------------|
| 0-3 | int32_t | Speed Motor 1 (mm/s) |
| 4-7 | int32_t | Speed Motor 2 (mm/s) |

#### 0x201: Position Feedback
Current motor positions.

| Byte | Data Type | Description |
|------|-----------|-------------|
| 0-3 | int32_t | Position Motor 1 (mm) |
| 4-7 | int32_t | Position Motor 2 (mm) |

#### 0x202: Battery & Temperature
System health data.

| Byte | Data Type | Description |
|------|-----------|-------------|
| 0-1 | int16_t | Battery voltage (0.01V units) |
| 2-3 | int16_t | Board temperature (°C) |

## Example Usage

### Arduino Example

```cpp
#include <mcp_can.h>
#include <SPI.h>

MCP_CAN CAN(10);  // CS pin

void setup() {
  if(CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK) {
    Serial.println("CAN Initialized");
    CAN.setMode(MCP_NORMAL);
  }
}

void loop() {
  // Send PWM command
  uint8_t data[8];
  int32_t pwm1 = 300;   // 30% forward
  int32_t pwm2 = -300;  // 30% backward
  
  memcpy(&data[0], &pwm1, 4);
  memcpy(&data[4], &pwm2, 4);
  
  CAN.sendMsgBuf(0x100, 0, 8, data);
  delay(20);
}
```

### Python Example with python-can

```python
import can

bus = can.interface.Bus(channel='can0', bustype='socketcan')

# Send PWM command
pwm1 = 300
pwm2 = -300

data = pwm1.to_bytes(4, 'little', signed=True) + \
       pwm2.to_bytes(4, 'little', signed=True)

msg = can.Message(arbitration_id=0x100, data=data, is_extended_id=False)
bus.send(msg)

# Receive status
msg = bus.recv(timeout=1.0)
if msg.arbitration_id == 0x200:
    speed1 = int.from_bytes(msg.data[0:4], 'little', signed=True)
    speed2 = int.from_bytes(msg.data[4:8], 'little', signed=True)
    print(f"Speed: {speed1}, {speed2} mm/s")
```

## Troubleshooting

### MCP2515 Not Detected

1. **Check wiring** - Verify all connections
2. **Check power** - MCP2515 needs 3.3V or 5V
3. **Check crystal** - Must be 8 MHz (default config)
4. **Check SPI communication** - Use oscilloscope on SCK/MOSI/MISO

### CAN Bus Errors

1. **Check termination** - 120Ω resistors at both ends of bus
2. **Check bitrate** - All devices must use same speed
3. **Check wiring** - CAN-H and CAN-L must not be swapped
4. **Check transceiver** - MCP2515 needs TJA1050/MCP2551

### Fallback to USART Mode

If MCP2515 not detected, firmware automatically uses USART2/USART3. No configuration changes needed.

## Source Files

- `inc/mcp2515.h` - MCP2515 driver header
- `src/mcp2515.c` - MCP2515 driver implementation
- `inc/software_spi.h` - Software SPI header
- `src/software_spi.c` - Software SPI implementation
- `inc/can_bus.h` - CAN protocol handler header
- `src/can_bus.c` - CAN protocol implementation
- `inc/config.h` - Configuration (CAN_CONTROLLED mode)
- `platformio.ini` - Build environment (control_CAN)

## Configuration Options

In `inc/config.h`:

```c
// CAN bus speed (500 kbps default)
#define CAN_SPEED MCP2515_SPEED_500KBPS

// Enable CAN bus support
#define ENABLE_CAN_BUS 1
```

## Technical Details

### Software SPI Implementation

- **Clock Speed:** ~1 MHz (configurable via delay)
- **Mode:** SPI Mode 0 (CPOL=0, CPHA=0)
- **Bit Order:** MSB first
- **Performance:** Sufficient for MCP2515 (max 10 MHz)

### Pin Sharing Strategy

The firmware uses GPIO reconfiguration to share pins:
- **Boot:** Configure as SPI, attempt MCP2515 detection
- **CAN Mode:** Keep pins configured as GPIO for software SPI
- **USART Mode:** Reconfigure pins as USART2_TX/RX, USART3_TX/RX

This approach allows seamless switching without jumpers or recompilation.

## License

Same as main bipropellant project - GPLv3

## Credits

- MCP2515 driver inspired by autowp/arduino-mcp2515
- Software SPI implementation for STM32F1xx
- Integration by GitHub Copilot CLI

---

**Questions?** Check the [main README](README.md) or open an issue.
