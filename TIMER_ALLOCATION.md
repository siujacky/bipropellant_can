# Timer Allocation Map - STM32F103RC

## 🎯 Current Timer Usage (CRITICAL - Do Not Modify Without Checking!)

### TIM1 - Motor Control (PWM)
- **Status**: ✅ ALLOCATED
- **Used by**: `setup.c` - Motor PWM generation
- **Purpose**: High-frequency PWM for motor control
- **Interrupt**: Yes (PWM update)
- **Priority**: CRITICAL (motor safety)
- **Notes**: DO NOT TOUCH - Motor control depends on this

### TIM2 - PPM/ADC Control Timing
- **Status**: ⚠️ CONDITIONALLY ALLOCATED
- **Used by**: `control.c` (if `CONTROL_PPM` defined)
- **Purpose**: RC PPM signal timing, ADC input timing
- **Interrupt**: Yes (PPM edge capture)
- **Priority**: HIGH (user input)
- **Notes**: 
  - CONFLICT RESOLVED: No longer used by Software Serial
  - Only allocated when PPM control is enabled
  - May also be used by ADC-based control modes

### TIM3 - Software Serial TX
- **Status**: ✅ ALLOCATED
- **Used by**: `softwareserial.c` - PA13/PA14 debug output
- **Purpose**: Bit-bang UART TX timing (115200 baud)
- **Interrupt**: Yes (TIM3_IRQHandler in softwareserial.c)
- **Frequency**: 115200 Hz (one interrupt per bit)
- **Priority**: NORMAL
- **Notes**: 
  - Timing-critical for UART output
  - Handler in softwareserial.c
  - Must run at precise intervals

### TIM4 - Hall Sensor Interrupts
- **Status**: ✅ ALLOCATED
- **Used by**: `hallinterrupts.c` - Motor position sensing
- **Purpose**: Hall effect sensor timing and debouncing
- **Interrupt**: Yes (TIM4_IRQHandler in hallinterrupts.c)
- **Priority**: HIGH (motor commutation)
- **Notes**: 
  - Critical for motor speed/position calculation
  - DO NOT TOUCH unless modifying motor control

### TIM5 - Software Serial RX
- **Status**: ✅ ALLOCATED (NEW - Phase 1 Fix)
- **Used by**: `softwareserial.c` - PA13/PA14 debug input
- **Purpose**: Free-running counter for RX bit timing
- **Interrupt**: No (polled via CNT register)
- **Frequency**: 115200 * 4 Hz (460800 Hz) for 4x oversampling
- **Priority**: N/A (no interrupt)
- **Notes**: 
  - **MIGRATED FROM TIM2** to resolve conflict with control.c
  - Used as timestamp reference, not interrupt-driven
  - 32-bit timer (better than TIM2's 16-bit for timestamps)

### TIM8 - Motor Control (PWM)
- **Status**: ✅ ALLOCATED
- **Used by**: `setup.c` - Motor PWM generation
- **Purpose**: High-frequency PWM for second motor channel
- **Interrupt**: Yes (PWM update)
- **Priority**: CRITICAL (motor safety)
- **Notes**: DO NOT TOUCH - Motor control depends on this

### TIM6 - Available
- **Status**: ⭕ FREE
- **Notes**: 
  - Basic timer (no PWM capability)
  - Could be used for general timing tasks
  - Good candidate for future features

### TIM7 - Available
- **Status**: ⭕ FREE
- **Notes**: 
  - Basic timer (no PWM capability)
  - Could be used for general timing tasks
  - Good candidate for future features

---

## 🔧 Phase 1 Fix Summary

### Problem:
- **TIM2 CONFLICT**: Both `control.c` and `softwareserial.c` tried to use TIM2
- Result: Software Serial timing broken when PPM/ADC control initialized
- Symptom: PA13/PA14 stopped working after boot

### Solution:
- **Migrated Software Serial RX from TIM2 to TIM5**
- TIM2: Now exclusively for PPM/ADC control
- TIM5: Now exclusively for Software Serial RX timing

### Why TIM5?
1. ✅ Unused in codebase
2. ✅ 32-bit timer (better timestamp range than TIM2's 16-bit)
3. ✅ General-purpose timer (suitable for free-running counter)
4. ✅ No conflicts with motor control or other peripherals

---

## 📋 Timer Allocation Rules

### Before Adding New Timer Usage:
1. **Check this document** - Verify timer is marked as FREE
2. **Search codebase** - `grep -r "TIMx" src/`
3. **Check config.h** - Look for conditional defines that might use the timer
4. **Update this document** - Document your new allocation

### Priority Hierarchy:
1. **CRITICAL**: Motor control, safety systems (TIM1, TIM8)
2. **HIGH**: User input, sensors, CAN (TIM2, TIM4)
3. **NORMAL**: Debug output, non-critical comms (TIM3, TIM5)
4. **LOW**: Optional features, diagnostics

### Choosing a Timer:
- **Need PWM?** → Use advanced timers (TIM1, TIM8) or general-purpose with PWM (TIM2-TIM5)
- **Need input capture?** → Use general-purpose timers (TIM2-TIM5)
- **Just need periodic interrupts?** → Basic timers okay (TIM6, TIM7)
- **Need free-running counter?** → Use 32-bit timers (TIM2, TIM5) for longer periods

---

## 🐛 Common Pitfalls

### 1. Shared Timer Initialization
**DON'T:**
```c
// Module A
__HAL_RCC_TIM2_CLK_ENABLE();
timer.Instance = TIM2;
HAL_TIM_Base_Init(&timer);

// Module B (later in boot)
__HAL_RCC_TIM2_CLK_ENABLE();  // Doesn't hurt
timer.Instance = TIM2;
HAL_TIM_Base_Init(&timer);  // ❌ OVERWRITES Module A's config!
```

**DO:**
```c
// Use different timers or coordinate via timer manager
```

### 2. Interrupt Priority Conflicts
**DON'T:**
```c
HAL_NVIC_SetPriority(TIM3_IRQn, 0, 0);  // Highest priority
HAL_NVIC_SetPriority(TIM1_IRQn, 0, 0);  // Also highest - may starve each other
```

**DO:**
```c
// Define hierarchy (see Phase 2)
#define PRIORITY_CRITICAL   0
#define PRIORITY_REALTIME   1
#define PRIORITY_HIGH       2
#define PRIORITY_NORMAL     3

HAL_NVIC_SetPriority(TIM1_IRQn, PRIORITY_CRITICAL, 0);  // Motor control
HAL_NVIC_SetPriority(TIM3_IRQn, PRIORITY_NORMAL, 0);    // Debug output
```

### 3. Clock Frequency Assumptions
**DON'T:**
```c
// Assumes 72 MHz, breaks if clock changes
softwareserialtimer.Init.Prescaler = 64000000 / 2 / 115200;
```

**DO:**
```c
// Use HAL_RCC_GetHCLKFreq() or HAL_RCC_GetPCLK1Freq()
uint32_t clock = HAL_RCC_GetPCLK1Freq();
softwareserialtimer.Init.Prescaler = clock / 115200 - 1;
```

---

## 🔄 Migration Guide (TIM2 → TIM5)

If you need to migrate another module from a conflicting timer:

### Step 1: Find All References
```bash
grep -rn "TIM2" src/your_module.c
grep -rn "TIM2_IRQHandler" src/
```

### Step 2: Update Initialization
```c
// Old
__HAL_RCC_TIM2_CLK_ENABLE();
timer.Instance = TIM2;

// New
__HAL_RCC_TIM5_CLK_ENABLE();
timer.Instance = TIM5;
```

### Step 3: Update Interrupt Handler (if any)
```c
// Old
void TIM2_IRQHandler(void) { ... }
HAL_NVIC_EnableIRQ(TIM2_IRQn);

// New
void TIM5_IRQHandler(void) { ... }
HAL_NVIC_EnableIRQ(TIM5_IRQn);
```

### Step 4: Update Comments/Documentation
- Update stm32f1xx_it.c timer comments
- Update this allocation document
- Add migration notes to commit message

---

## 📊 Verification Checklist

After modifying timer allocations:

- [ ] Compiled successfully
- [ ] No duplicate timer usage
- [ ] Interrupts fire at expected rate
- [ ] No timing glitches (measure with oscilloscope)
- [ ] Motors run smoothly (if applicable)
- [ ] Serial communication works (if applicable)
- [ ] PPM input works (if applicable)
- [ ] Updated this document

---

## 🚀 Future Improvements (Phase 2+)

1. **Timer Manager**
   - Centralized timer allocation
   - Prevents conflicts at compile time
   - Runtime timer sharing coordination

2. **Dynamic Priority Adjustment**
   - Adjust interrupt priorities based on mode
   - E.g., lower debug output priority during critical maneuvers

3. **Timer Utilization Monitoring**
   - Track timer load/CPU usage
   - Warn if timing becomes unstable

---

*Last Updated: 2026-01-18 (Phase 1: TIM5 Migration)*
*Next Update: Phase 2 (Interrupt Priority Hierarchy)*
