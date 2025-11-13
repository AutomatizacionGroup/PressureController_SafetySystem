# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Pressure Controller Safety System v3.1 - An advanced Arduino-based pressure regulation system with comprehensive sensor fault detection, automatic pump protection, and safety interlocks. Targets Arduino Nano ESP32 (primary) with fallback support for Arduino AVR boards.

**Core Focus**: Safety-first design with non-blocking control loop, automatic failure detection, and configurable sensor implementations.

## Build & Deployment Commands

### Compilation
```bash
# For ESP32 Nano (primary platform)
arduino-cli compile --fqbn esp32:esp32:esp32s3 PressureController_SafetySystem.ino

# For Arduino AVR fallback (Nano/Uno - memory constrained)
arduino-cli compile --fqbn arduino:avr:nano PressureController_SafetySystem.ino
```

### Upload to Device
```bash
# ESP32 Nano on COM port
arduino-cli upload -p COM13 --fqbn esp32:esp32:esp32s3

# For first-time ESP32 upload or connection issues:
# Hold BOOT button, press RESET, release BOOT, then upload
```

### Serial Monitoring
```bash
# View debug output (115200 baud)
arduino-cli monitor -p COM13 --config baudrate=115200
```

## Project Structure

```
PressureController_SafetySystem/
├── PressureController_SafetySystem.ino    # Main sketch (1800+ lines)
├── README.md                              # User guide & hardware specs
├── CAMBIOS_v3.1.md                        # Detailed changelog
├── GUIA_RAPIDA.md                         # Quick start guide
└── CLAUDE.md                              # This file
```

## Architecture Overview

### Core Design Patterns

**1. Non-Blocking Timing Loop**
- Main loop runs continuously with timing gates via `millis()` comparisons
- Prevents `delay()` blocking entire system
- Updates happen at intervals: OPERATION_UPDATE_INTERVAL (100ms), DISPLAY_UPDATE_INTERVAL (200ms), SENSOR_CHECK_INTERVAL (50ms)
- See: `operationMode()` function and `sysState` struct

**2. Safety-First State Machine**
- Three modes: `MODE_OPERATION`, `MODE_MENU_LIST`, `MODE_MENU_CONFIG`
- `pumpBlocked` flag gates all pump activation - no exceptions
- Multiple independent safety conditions block pump: sensor errors, open inlet valve, low tank level
- See: Control logic in `operationMode()` around line 965

**3. Sensor Validation with Hysteresis**
- Function `checkSensor()` performs voltage range detection + erratic change detection
- Erratic detection: if ADC change > ADC_MAX_CHANGE, enter confirmation phase (ERRATIC_CONFIRMATION_TIME = 3s, need 3 samples)
- Prevents false positives from electrical noise
- See: lines 285-372

**4. Digital Input Monitoring**
- D7 (INPUT_PIN): Inlet valve sensor
  - LOW = valve open, HIGH = valve closed
  - Includes fault detection: if stuck HIGH for >10s while valveImplemented=true, triggers sensor disconnection
  - Function: `checkInletValve()` (lines 467-542)

- D6 (TANK_LEVEL_PIN): Tank low-level sensor
  - LOW = tank has water, HIGH = tank empty/low
  - Simple state change detection with 100ms debounce
  - Function: `checkTankLevel()` (lines 544-578)

**5. Configuration Persistence with Version Control**
- ESP32: Uses `Preferences` library (key-value store)
- AVR: Uses `EEPROM` with CRC-8 checksum validation
- **Version Control**: `#define CONFIG_VERSION 2` detects struct changes automatically
- Config struct contains: version, pressMin/Max, setpoint, deadband, minOnTime, valveImplemented, tankSensorImplemented, checksum
- **Auto-Recovery**: If stored version != CONFIG_VERSION, automatically regenerates defaults (handles EEPROM format changes)
- Save/Load functions: `loadConfig()`, `saveConfig()`, `loadDefaults()`, `clearStoredConfig()`
- **Critical**: When struct Config changes, increment CONFIG_VERSION to prevent data corruption
- Serial output shows version on load/save (e.g., "Configuracion cargada desde Preferences (v2)")

**6. Menu System with Scroll**
- 6 menus (0-indexed as menuSelection): Rango (1), Punto ajuste (2), Banda muerta (3), Tiempo min (4), Valvula (5), Sensor tanque (6), SALIR (7)
- Scroll display: Shows max 5 items, auto-scrolls when menuSelection > 2
- Encoder rotation with debounce (ENCODER_DEBOUNCE_TIME = 5ms)
- See: `menuListMode()` (lines 1194-1271) and `handleShortPress()` (lines 880-909)

### Pin Mapping
```
D2:  ENCODER_CLK (input, pullup)
D3:  ENCODER_DT (input, pullup)
D4:  ENCODER_SW (input, pullup)
D6:  TANK_LEVEL_PIN (input) - LOW=ok, HIGH=low
D7:  INPUT_PIN (input) - LOW=open valve, HIGH=closed valve
D10: PUMP_PIN (output, PWM)
D12: LED_PIN (output)
A0:  ANALOG_PIN (analog input) - Pressure sensor
```

### Key Constants
- ADC resolution: ESP32=4095 (12-bit), AVR=1023 (10-bit)
- Sensor thresholds (ADC values vary by platform)
- Debounce times: INLET_DEBOUNCE_TIME=100ms, TANK_DEBOUNCE_TIME=100ms, ENCODER_DEBOUNCE_TIME=5ms
- Recovery cycle count: RECOVERY_CYCLES=10 good readings needed to recover from error

## Critical Implementation Details

### Important: No Accents in Display/Serial
- All text uses ASCII-only characters (removed all áéíóú)
- This prevents display corruption from encoding issues
- Examples: "Valvula" not "Válvula", "Señal" not "Señal"

### Pump Control Logic (Line 968)
```cpp
if (pumpBlocked || inletValveOpen || tankLevelLow) {
  // Pump MUST be OFF - multiple safety interlocks
  digitalWrite(PUMP_PIN, LOW);
}
else {
  // Normal hysteresis control only if all safety conditions met
}
```

### Display Layout
- Line 14: Pressure value (large text, textSize=2)
- Line 30: V:OK/V:ABT/V:ERR (left), T:OK/T:BAJO (right) - compact 5-char max per indicator
- Line 40: Separator line
- Line 44: Configuration values (setpoint, deadband, time)
- Line 54: Pressure bar graph with setpoint marker

### Valve Fault Detection (D7)
- Monitors if D7 HIGH for > VALVE_FAULT_THRESHOLD (10s) with NO state changes
- Indicates sensor likely disconnected despite config saying valveImplemented=true
- Triggers pumpBlocked=true as safety measure
- Auto-recovers when D7 goes LOW (sensor responds)
- See: `checkInletValve()` lines 492-527

### Configuration Menu Flow
1. Long press encoder: Enter MODE_MENU_LIST
2. Rotate to select menu (0-6)
3. Short press: Enter MODE_MENU_CONFIG for selected menu
4. Press again to edit value
5. Rotate to adjust, press to save
6. Returns to MODE_MENU_LIST
7. Long press to exit back to MODE_OPERATION

## Common Development Tasks

### Adding New Digital Input
1. Define pin macro (e.g., `#define NEW_PIN 5`)
2. Add `pinMode(NEW_PIN, INPUT)` in `setup()`
3. Create check function with debounce (copy pattern from `checkInletValve()`)
4. Call check function from `operationMode()` after sensor validation
5. Add safety condition to pump control if needed

### Adding New Menu Item
1. Add to Config struct
2. Add default constant
3. Create `displayMenuX()` function
4. Add case in `menuConfigMode()` switch
5. Update menu items/icons arrays in `menuListMode()` (increment loop count)
6. Update `handleShortPress()` menuSelection limit check
7. Add save logic in `handleMenuNavigation()`

### IMPORTANT: When Modifying Config Struct
1. **ALWAYS increment `CONFIG_VERSION`** at top of file (e.g., `#define CONFIG_VERSION 3`)
2. Add new fields to Config struct
3. Add defaults (e.g., `const bool DEFAULT_NEW_FEATURE = true`)
4. Update `loadConfig()` to read new fields (both ESP32 Preferences AND AVR EEPROM)
5. Update `saveConfig()` to write new fields
6. Update `loadDefaults()` to initialize new fields with defaults
7. System will auto-detect version mismatch and regenerate defaults (no manual reset needed!)
8. This prevents data corruption when EEPROM format changes

### Debugging Sensor Issues
- Serial output at 115200 baud shows real-time ADC values every 5s
- Check ADC_DISCONNECTED / ADC_HIGH_WARNING thresholds in platform-specific section
- Use Serial Monitor to verify sensor voltage readings
- Enable hysteresis confirmation counter (`erraticConfirmationCount`) in debug

## Platform-Specific Notes

### ESP32 Nano S3
- ADC: 12-bit (0-4095), 3.3V reference
- Requires voltage divider 1:1 if sensor outputs 0-5V
- Preferences library handles SPIFFS auto-management
- Native USB for uploads (no CH340 driver needed)

### Arduino AVR (Nano/Uno)
- ADC: 10-bit (0-1023), 5V reference
- Sensor 0-5V maps directly
- EEPROM limited (1024 bytes) - keep Config struct small
- Code size is tight (memory-constrained) - may not fit with serial debugging enabled

## Safety-Critical Notes

- Pump control has NO exceptions: blocked state is absolute
- Sensor disconnection detection on D7/D6 prevents dry-run conditions
- Tank level sensor prevents pump cavitation
- LED pulsates during any alarm condition for visual feedback
- Recovery requires 10 consecutive good cycles after error
- All timing is non-blocking to prevent watchdog resets

## Testing Checklist

Before deployment:
- [ ] Sensor reads correct ADC values at min/max pressure
- [ ] Pump blocks immediately when alarm triggered
- [ ] Encoder navigation works without skipping
- [ ] Menu 5 (valve config) can enable/disable
- [ ] Menu 6 (tank sensor config) can enable/disable
- [ ] Valve fault detection triggers after ~10s if D7 disconnected
- [ ] Tank low detection blocks pump when D6 goes HIGH
- [ ] Configuration persists after power cycle
- [ ] Serial monitor shows clean debug output (no garbled chars)
- [ ] Display shows no overlapping text
