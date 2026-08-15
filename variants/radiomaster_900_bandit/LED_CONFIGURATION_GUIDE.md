# LED Color Configuration Guide

## Overview

Your RadioMaster Bandit now has **two user-configurable LED colors**:

1. **TX_LED_COLOR** - Color during radio transmission (flash)
2. **NEW_MSG_LED** - Color for new message notifications (breathing effect)

## Quick Setup

### platformio.ini Configuration

```ini
[env:bandit]
build_flags = 
    -DTX_LED_COLOR=0x00FF00      ; Green transmission flash (default)
    -DNEW_MSG_LED=0x0000FF       ; Blue message breathing (default)
```

### Example Configurations

**Default (Green TX, Blue Messages):**
```ini
build_flags = 
    -DTX_LED_COLOR=0x009600      ; Green (0, 150, 0) - slightly dimmed
    -DNEW_MSG_LED=0x0000FF       ; Blue
```

**All Red:**
```ini
build_flags = 
    -DTX_LED_COLOR=0xFF0000      ; Red transmission
    -DNEW_MSG_LED=0xFF0000       ; Red messages
```

**High Contrast (Red TX, Blue Messages):**
```ini
build_flags = 
    -DTX_LED_COLOR=0xFF0000      ; Red transmission
    -DNEW_MSG_LED=0x0000FF       ; Blue messages
```

**Professional (Blue TX, Cyan Messages):**
```ini
build_flags = 
    -DTX_LED_COLOR=0x0000FF      ; Blue transmission
    -DNEW_MSG_LED=0x00FFFF       ; Cyan messages
```

**Stealth (Dim Purple for both):**
```ini
build_flags = 
    -DTX_LED_COLOR=0x400040      ; Dim purple transmission
    -DNEW_MSG_LED=0x8000FF       ; Purple messages
```

**High Visibility (Yellow TX, Magenta Messages):**
```ini
build_flags = 
    -DTX_LED_COLOR=0xFFFF00      ; Yellow transmission
    -DNEW_MSG_LED=0xFF00FF       ; Magenta messages
```

## Color Behavior

### TX_LED_COLOR (Transmission Flash)
- **When**: During radio transmission
- **NeoPixels**: 2-5 (indices 2, 3, 4, 5)
- **Duration**: Brief flash while transmitting
- **Effect**: Solid color, no animation
- **Default**: Green (0x009600 = RGB 0, 150, 0)

### NEW_MSG_LED (Message Notification)
- **When**: Unread messages exist
- **NeoPixels**: 2-5 (indices 2, 3, 4, 5)
- **Duration**: Continuous until messages read
- **Effect**: Smooth breathing/pulsing
- **Default**: Blue (0x0000FF)

### NeoPixel Layout
```
Index 0: Button 1 backlight (not controlled by these settings)
Index 1: Button 2 backlight (not controlled by these settings)
Index 2: TX flash / Message notification
Index 3: TX flash / Message notification
Index 4: TX flash / Message notification
Index 5: TX flash / Message notification
```

## Color Format

Both use RGB hex format: `0xRRGGBB`
- RR = Red (00-FF hex / 0-255 decimal)
- GG = Green (00-FF hex / 0-255 decimal)
- BB = Blue (00-FF hex / 0-255 decimal)

### Common Colors

| Color | Hex | Decimal RGB |
|-------|-----|-------------|
| Red | `0xFF0000` | 255, 0, 0 |
| Green | `0x00FF00` | 0, 255, 0 |
| Blue | `0x0000FF` | 0, 0, 255 |
| Yellow | `0xFFFF00` | 255, 255, 0 |
| Cyan | `0x00FFFF` | 0, 255, 255 |
| Magenta | `0xFF00FF` | 255, 0, 255 |
| White | `0xFFFFFF` | 255, 255, 255 |
| Orange | `0xFF8000` | 255, 128, 0 |
| Purple | `0x8000FF` | 128, 0, 255 |

### Brightness Control

You can dim colors by reducing the values:

**Full Brightness Green:** `0x00FF00` (0, 255, 0)
**Medium Green:** `0x008000` (0, 128, 0)
**Dim Green:** `0x004000` (0, 64, 0)
**Very Dim Green:** `0x002000` (0, 32, 0)

**Current default TX color:** `0x009600` (0, 150, 0) - medium-bright green

## Use Case Examples

### Emergency Services / High Visibility
```ini
build_flags = 
    -DTX_LED_COLOR=0xFF0000      ; Red transmission (urgent)
    -DNEW_MSG_LED=0xFF0000       ; Red messages (urgent)
```

### Professional / Office
```ini
build_flags = 
    -DTX_LED_COLOR=0x004080      ; Dim blue transmission
    -DNEW_MSG_LED=0x0000FF       ; Blue messages
```

### Battery Saver (Single color LEDs)
```ini
build_flags = 
    -DTX_LED_COLOR=0x00FF00      ; Green only (1 LED per pixel)
    -DNEW_MSG_LED=0x00FF00       ; Green only (1 LED per pixel)
```

### Nighttime / Low Light
```ini
build_flags = 
    -DTX_LED_COLOR=0x400000      ; Very dim red
    -DNEW_MSG_LED=0x400000       ; Very dim red
```

### Color-Coded Operations
```ini
build_flags = 
    -DTX_LED_COLOR=0xFFFF00      ; Yellow = transmitting
    -DNEW_MSG_LED=0x00FF00       ; Green = new message received
```

### Rainbow Theme
```ini
build_flags = 
    -DTX_LED_COLOR=0xFF00FF      ; Magenta TX
    -DNEW_MSG_LED=0x00FFFF       ; Cyan messages
```

## Recommended Color Combinations

### Same Color (Unified Look)
- Both Blue: `TX=0x0000FF, MSG=0x0000FF`
- Both Green: `TX=0x00FF00, MSG=0x00FF00`
- Both Red: `TX=0xFF0000, MSG=0xFF0000`

### Contrasting (Easy to Distinguish)
- Red TX, Blue MSG: `TX=0xFF0000, MSG=0x0000FF`
- Green TX, Magenta MSG: `TX=0x00FF00, MSG=0xFF00FF`
- Yellow TX, Cyan MSG: `TX=0xFFFF00, MSG=0x00FFFF`

### Subtle (Professional)
- Dim Blue TX, Blue MSG: `TX=0x000080, MSG=0x0000FF`
- Dim Green TX, Cyan MSG: `TX=0x004000, MSG=0x00FFFF`
- Dim Purple TX, Purple MSG: `TX=0x400040, MSG=0x8000FF`

## Power Consumption Notes

**Most Efficient (Single LED color):**
- Red: `0xFF0000`
- Green: `0x00FF00`
- Blue: `0x0000FF`

**Medium Efficiency (Two LED colors):**
- Yellow: `0xFFFF00` (Red + Green)
- Cyan: `0x00FFFF` (Green + Blue)
- Magenta: `0xFF00FF` (Red + Blue)

**Least Efficient (All LEDs):**
- White: `0xFFFFFF` (Red + Green + Blue)

Lower brightness values also save power:
- `0x00FF00` = 255 brightness
- `0x008000` = 128 brightness (half power)
- `0x004000` = 64 brightness (quarter power)

## Testing Your Configuration

1. Set your colors in `platformio.ini`
2. Build and flash: `pio run -t upload`
3. **Test TX LED**: Send a message - should see brief flash in TX_LED_COLOR
4. **Test MSG LED**: Receive a message - should see breathing effect in NEW_MSG_LED
5. Adjust brightness/colors as needed

## Troubleshooting

**Colors look wrong:**
- Verify hex format: `0xRRGGBB` with `0x` prefix
- Check build flags are being applied (view compiler output)
- NeoPixels are GRB format, but code handles conversion

**TX flash too bright:**
- Reduce TX_LED_COLOR values (e.g., `0x00FF00` → `0x008000`)

**Message breathing too dim:**
- Adjust `NEOPIXEL_MAX_BRIGHTNESS` in UITask.cpp (default: 80)

**Want different brightness for TX vs MSG:**
- TX: Adjust the color value directly (e.g., `0x009600` is dimmer than `0x00FF00`)
- MSG: Adjust `NEOPIXEL_MAX_BRIGHTNESS` in code

## Advanced: Hex to RGB Conversion

If you want a specific RGB value:

**Formula:** `0xRRGGBB`

**Example - RGB(255, 128, 64) = Orange:**
1. Red = 255 = 0xFF
2. Green = 128 = 0x80
3. Blue = 64 = 0x40
4. Result = `0xFF8040`

**Hex conversion chart:**
```
0 = 0x00      64 = 0x40     128 = 0x80    192 = 0xC0
32 = 0x20     96 = 0x60     160 = 0xA0    224 = 0xE0
48 = 0x30     112 = 0x70    176 = 0xB0    255 = 0xFF
```

## Files Modified

1. **BanditBoard.h** - Added TX_LED_COLOR support
2. **UITask.h** - NeoPixel message notification support
3. **UITask.cpp** - NEW_MSG_LED color system

---

## Quick Copy-Paste Examples

### Default Configuration
```ini
-DTX_LED_COLOR=0x009600 -DNEW_MSG_LED=0x0000FF
```

### All Green
```ini
-DTX_LED_COLOR=0x00FF00 -DNEW_MSG_LED=0x00FF00
```

### All Red
```ini
-DTX_LED_COLOR=0xFF0000 -DNEW_MSG_LED=0xFF0000
```

### Red TX, Blue Messages
```ini
-DTX_LED_COLOR=0xFF0000 -DNEW_MSG_LED=0x0000FF
```

### Yellow TX, Magenta Messages
```ini
-DTX_LED_COLOR=0xFFFF00 -DNEW_MSG_LED=0xFF00FF
```

### Dim Everything (Stealth)
```ini
-DTX_LED_COLOR=0x202020 -DNEW_MSG_LED=0x404040
```
