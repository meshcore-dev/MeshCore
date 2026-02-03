# Display and LED Control for Heltec V3

The Heltec V3 variant now supports user-configurable settings to enable/disable the screen and LED lights through CLI commands.

## Available Settings

### 1. Screen Control (Repeaters/Clients with Display)
- **Setting name**: `screen`
- **Values**: `0` (disabled) or `1` (enabled)
- **Default**: Enabled
- **Purpose**: Control the OLED display power

### 2. LED Control (All devices)
- **Setting name**: `led`
- **Values**: `0` (disabled) or `1` (enabled)  
- **Default**: Enabled
- **Purpose**: Control the TX indicator LED (pin 35)

## CLI Commands

### View Current Settings
```bash
sensor list
```
This will show all available settings and their current values.

### Get Specific Setting
```bash
sensor get screen
sensor get led
```

### Change Settings
```bash
# Disable screen
sensor set screen 0

# Enable screen
sensor set screen 1

# Disable LED
sensor set led 0

# Enable LED
sensor set led 1
```

## Using in Application Code

### Check if Screen is Enabled
```cpp
#include "target.h"

void setup() {
  board.begin();
  
  #ifdef DISPLAY_CLASS
    if (board.screen_enabled && display.begin()) {
      // Display is enabled and initialized
      display.startFrame();
      display.print("Screen is on!");
      display.endFrame();
    }
  #endif
}
```

### Control LED with Settings
```cpp
void onTransmit() {
  // LED will only turn on if led_enabled is true
  board.setLED(true);
  delay(100);
  board.setLED(false);
}
```

### Disable Display at Runtime
```cpp
void loop() {
  #ifdef DISPLAY_CLASS
    if (!board.screen_enabled && display.isOn()) {
      display.turnOff();  // Turn off if setting was changed
    } else if (board.screen_enabled && !display.isOn()) {
      display.turnOn();   // Turn on if setting was changed
    }
  #endif
}
```

## Power Saving Benefits

Disabling the screen and LED can significantly reduce power consumption:
- **Screen**: ~15-20mA when active
- **LED**: ~5-10mA when lit
- **Combined savings**: Up to 30mA when both disabled

This is especially useful for:
- Solar-powered nodes
- Battery-operated devices
- Stealth deployments where visual indicators are not desired

## Hardware Details

- **Screen Power Pin**: GPIO 36 (`PIN_VEXT_EN`)
- **LED Pin**: GPIO 35 (`P_LORA_TX_LED`)
- **Display Type**: SSD1306 OLED (128x64)
- **I2C Address**: 0x3C

## Notes

1. Settings are managed through the board's `getNumSettings()`, `getSettingName()`, `getSettingValue()`, and `setSettingValue()` methods
2. The display power is controlled via the `RefCountedDigitalPin periph_power` to safely share power control with other peripherals
3. LED control uses direct GPIO manipulation for minimal overhead
4. Settings can be persisted by saving node preferences after modification
