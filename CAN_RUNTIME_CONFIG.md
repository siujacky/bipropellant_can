# CAN Runtime Configuration - Implementation Complete

## Summary

Successfully implemented runtime configuration for CAN Board ID and Base IDs using the existing flash storage system. Users can now configure CAN parameters through serial commands without recompiling firmware.

## What Was Changed

### 1. Flash Storage Structure (inc/flashcontent.h)
Added three new fields to FLASH_CONTENT structure:
- CAN_BoardID - Board ID (0-15)
- CAN_BaseID_CMD - Command base CAN ID (default: 0x100)
- CAN_BaseID_STATUS - Status base CAN ID (default: 0x200)

### 2. Default Values (src/main.c)
Added defaults to FlashDefaults structure:
`c
.CAN_BoardID = CAN_BOARD_ID,        // From config.h
.CAN_BaseID_CMD = 0x100,            // Command base ID
.CAN_BaseID_STATUS = 0x200,         // Status base ID
`

### 3. CAN Bus Logic (src/can_bus.c)
Modified CAN ID initialization to use flash-stored values:
- Reads Board ID from FlashContent.CAN_BoardID
- Reads base IDs from FlashContent.CAN_BaseID_CMD and CAN_BaseID_STATUS
- Calculates actual CAN IDs: BaseID + (BoardID × 0x10) + offset
- Added CAN_PrintConfiguration() to display settings on boot

### 4. Protocol Functions (src/protocolfunctions.c)
Exposed CAN parameters through protocol system:
- Parameter 0xC0: CAN_BoardID
- Parameter 0xC1: CAN_BaseID_CMD
- Parameter 0xC2: CAN_BaseID_STATUS

All use n_FlashContentMagic handler for flash write on magic number (1238).

### 5. Documentation (CAN_BOARD_ID_GUIDE.md)
Added "Method 4: Runtime Serial Configuration" with:
- Step-by-step instructions
- Example commands
- Protocol parameter table
- Boot configuration display example

## How to Use

### Reading Configuration
`
rC0   // Read Board ID
rC1   // Read Command Base ID
rC2   // Read Status Base ID
`

### Writing Configuration
`
wC0 2        // Set Board ID to 2
wC1 256      // Set Command Base to 0x100 (256 decimal)
wC2 512      // Set Status Base to 0x200 (512 decimal)
w80 1238     // Save to flash (magic number)
`

### Verify on Next Boot
After power cycle, board prints:
`
=== CAN Configuration ===
Board ID: 2
Base CMD ID: 0x100
Base STATUS ID: 0x200

Command IDs:
  PWM:      0x120
  Speed:    0x121
  Position: 0x122
  Enable:   0x123

Status IDs:
  Speed:    0x220
  Position: 0x221
  Battery:  0x222
========================
`

## Benefits

1. **No Recompilation** - Change Board IDs through serial interface
2. **Field Configurable** - Update multiple boards on-site
3. **Persistent Storage** - Settings survive power cycles
4. **Custom Base IDs** - Adapt to existing CAN networks
5. **Visual Feedback** - Boot messages confirm configuration

## Implementation Details

### Flash Storage Integration
- Uses existing flash storage system (last 4KB of flash)
- Follows 2-byte alignment (pragma pack(2))
- Wear leveling via sequential writes
- Commit on magic number write (0x80 = 1238)

### ID Calculation
`c
uint8_t board_id = FlashContent.CAN_BoardID & 0x0F;  // 0-15
uint32_t offset = board_id * 0x10;

// Command IDs
can_cmd_pwm = FlashContent.CAN_BaseID_CMD + offset + 0x00;
can_cmd_speed = FlashContent.CAN_BaseID_CMD + offset + 0x01;
can_cmd_position = FlashContent.CAN_BaseID_CMD + offset + 0x02;
can_cmd_enable = FlashContent.CAN_BaseID_CMD + offset + 0x03;

// Status IDs
can_status_speed = FlashContent.CAN_BaseID_STATUS + offset + 0x00;
can_status_pos = FlashContent.CAN_BaseID_STATUS + offset + 0x01;
can_status_batt = FlashContent.CAN_BaseID_STATUS + offset + 0x02;
`

### Fallback Behavior
- If FlashContent unprogrammed (0xFFFF), uses #define CAN_BOARD_ID from config.h
- Ensures compatibility with compile-time configuration

## Testing Checklist

- [ ] Read parameters via serial (rC0, rC1, rC2)
- [ ] Write Board ID (wC0 X)
- [ ] Write Base IDs (wC1, wC2)
- [ ] Commit to flash (w80 1238)
- [ ] Verify boot message shows new configuration
- [ ] Test CAN communication with new IDs
- [ ] Verify flash persistence across power cycles
- [ ] Test multiple boards with different IDs on same bus

## Files Modified (5)

1. inc/flashcontent.h - Added CAN config fields
2. src/main.c - Added default values and boot print
3. src/can_bus.c - Read from flash, calculate IDs, print config
4. src/protocolfunctions.c - Expose parameters 0xC0-0xC2
5. CAN_BOARD_ID_GUIDE.md - Document serial commands

## Status

✅ **Implementation Complete**
✅ **Documentation Updated**
⏳ **Hardware Testing Pending**

Ready for firmware build and hardware validation.
