# MCP2515 Wiring Diagram

## Pin Connections

```
┌─────────────────────────────────────────────────────────────────┐
│                    STM32F103RC Hoverboard                       │
│                                                                 │
│  PA2 (USART2_TX) ─────────────┐                               │
│  PA3 (USART2_RX) ─────────┐   │                               │
│  PB10 (USART3_TX) ─────┐  │   │                               │
│  PB11 (USART3_RX) ───┐ │  │   │                               │
│  PB12 (Free GPIO) ─┐ │ │  │   │                               │
│  3.3V/5V ────────┐ │ │ │  │   │                               │
│  GND ──────────┐ │ │ │ │  │   │                               │
└────────────────┼─┼─┼─┼─┼──┼───┼───────────────────────────────┘
                 │ │ │ │ │  │   │
                 │ │ │ │ │  │   │
┌────────────────┼─┼─┼─┼─┼──┼───┼───────────────────────────────┐
│                │ │ │ │ │  │   │    MCP2515 CAN Module         │
│                │ │ │ │ │  │   │                               │
│          GND ──┘ │ │ │ │  │   │                               │
│          VCC ────┘ │ │ │  │   │    ┌──────────────────┐      │
│          INT ──────┘ │ │  │   │    │                  │      │
│          CS ─────────┘ │  │   │    │    MCP2515       │      │
│          SCK ──────────┘  │   │    │   CAN Controller │      │
│          MISO ────────────┘   │    │                  │      │
│          MOSI ────────────────┘    │   8 MHz Crystal  │      │
│                                     │                  │      │
│                            ┌───────┤ CANH    CANL  ├──┐      │
│                            │       └──────────────────┘  │      │
└────────────────────────────┼──────────────────────────┼──────┘
                             │                          │
                             │   CAN Bus Cable          │
                             │   (Twisted Pair)         │
                             │                          │
┌────────────────────────────┼──────────────────────────┼──────┐
│                            │    CAN Transceiver       │       │
│                        ┌───┤    (TJA1050/MCP2551)  ├──┐      │
│                        │   └──────────────────────┘  │       │
│                    CANH│                          CANL│       │
│                        │                             │       │
│                   [120Ω Termination Resistor]        │       │
│                        │                             │       │
│                    Other CAN Devices on Bus...       │       │
└─────────────────────────────────────────────────────────────┘
```

## Detailed Pin Table

| Signal | STM32 Pin | MCP2515 Pin | Function | Direction |
|--------|-----------|-------------|----------|-----------|
| MOSI | PA2 | SI (D11) | SPI Data Out | STM32 → MCP2515 |
| MISO | PA3 | SO (D12) | SPI Data In | MCP2515 → STM32 |
| SCK | PB10 | SCK (D13) | SPI Clock | STM32 → MCP2515 |
| CS | PB11 | CS (D10) | Chip Select | STM32 → MCP2515 |
| INT | PB12 | INT (D2) | Interrupt | MCP2515 → STM32 |
| VCC | 3.3V or 5V | VCC | Power | Supply |
| GND | GND | GND | Ground | Common |

## MCP2515 Module Pin Labels

Most MCP2515 modules use Arduino-style pin labels:

```
┌─────────────────────────┐
│    MCP2515 CAN Module   │
│                         │
│  VCC  GND  CS  SO  SI   │  ← Top row
│                         │
│  SCK  INT  RST CANH CANL│  ← Bottom row
└─────────────────────────┘
```

**Connection:**
- VCC → 3.3V or 5V (module dependent)
- GND → Ground
- CS → PB11
- SO (MISO) → PA3
- SI (MOSI) → PA2
- SCK → PB10
- INT → PB12 (optional but recommended)
- RST → Leave unconnected (internal pull-up)
- CANH/CANL → To CAN bus

## CAN Bus Topology

### Simple 2-Device Setup
```
   [STM32 Hoverboard]          [CAN Controller]
          |                           |
      MCP2515                     MCP2515
          |                           |
    ┌─────┴────┐                ┌────┴─────┐
    │          │                │          │
  CANH      CANL              CANH      CANL
    │          │                │          │
    ├─ 120Ω ──┤                ├─ 120Ω ──┤  ← Termination
    │          │                │          │
    └────┬─────┘                └────┬─────┘
         └─────── CAN Bus ───────────┘
           (Twisted pair wire)
```

### Multi-Device CAN Network
```
[Device 1]    [Device 2]    [Device 3]    [Device 4]
    |             |             |             |
  120Ω           No           No           120Ω  ← Only at ends!
    |             |             |             |
    └─────────────┴─────────────┴─────────────┘
              CAN Bus (H and L)
```

**Important:** Only add 120Ω termination at the **two ends** of the bus!

## MCP2515 Module Variants

### Type 1: Arduino Shield Style
```
Pin order: VCC GND CS SO SI SCK INT RST CANH CANL
```

### Type 2: Breakout Board
```
Pin order: VCC GND MOSI MISO SCK CS INT CANH CANL
```

### Type 3: Waveshare Module
```
Left side: VCC GND SCK SI SO CS
Right side: INT RST CANH CANL
```

**Always check your module pinout before connecting!**

## Power Supply Notes

### 3.3V Logic Level
- Most STM32 boards run at 3.3V
- MCP2515 can operate at 3.3V
- Voltage translators NOT needed if module is 3.3V

### 5V Logic Level
- Some MCP2515 modules require 5V VCC
- Logic signals may be 5V tolerant on STM32
- Check STM32F103 datasheet for 5V tolerance on PA2/PA3/PB10/PB11

### Recommendation
Use a **3.3V MCP2515 module** for direct compatibility with STM32.

## Cable Specifications

### CAN Bus Cable
- **Type:** Twisted pair
- **Impedance:** 120Ω characteristic impedance
- **Max Length:** 
  - 40 meters @ 1 Mbps
  - 100 meters @ 500 kbps
  - 500 meters @ 125 kbps

### Recommended Cables
- CAT5/CAT6 Ethernet cable (use one twisted pair)
- Dedicated CAN bus cable (DeviceNet, CANopen)
- 2-conductor twisted pair 22-24 AWG

## Common Mistakes

❌ **Wrong:** Connecting 5V to 3.3V-only STM32 pins  
✅ **Right:** Use 3.3V module or level shifters

❌ **Wrong:** No termination resistors  
✅ **Right:** 120Ω at both ends of bus

❌ **Wrong:** Termination at every device  
✅ **Right:** Only at the two ends

❌ **Wrong:** Using long untwisted wires  
✅ **Right:** Twisted pair for noise immunity

❌ **Wrong:** Swapping CANH and CANL  
✅ **Right:** CANH to CANH, CANL to CANL

## Testing Setup

Minimal test configuration:
```
PC/Arduino ──[USB-CAN]── CAN Bus ──[MCP2515]── STM32 Hoverboard
              120Ω                    120Ω
```

Tools needed:
- USB-CAN adapter (PCAN, CANable, etc.)
- CAN analysis software (PCAN View, Cangaroo, etc.)
- 2x 120Ω resistors
- Twisted pair wire

## Oscilloscope Verification

When debugging, probe these signals:
1. **SCK** (PB10) - Should show clock pulses during SPI transfers
2. **MOSI** (PA2) - Data being sent to MCP2515
3. **MISO** (PA3) - Data received from MCP2515
4. **CS** (PB11) - Active low during transfers
5. **CANH/CANL** - Differential CAN signal (~2V differential)

Expected at 500 kbps CAN:
- Bit time: 2 µs
- Voltage swing: 1.5-3.5V (CANH), 0.5-1.5V (CANL)
- Differential: 2V typical

---

**Need help?** Check the main [CAN_BUS_README.md](CAN_BUS_README.md) for detailed documentation.
