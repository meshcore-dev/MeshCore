# Quick Reference: NeoPixel LED Configuration

## 🎨 Two Configurable LED Colors

### TX_LED_COLOR - Radio Transmission Flash
Color shown on NeoPixels 2-5 during transmission

### NEW_MSG_LED - Message Notification Breathing
Color shown on NeoPixels 2-5 when you have unread messages

## ⚡ Quick Setup

### platformio.ini
```ini
build_flags = 
    -DTX_LED_COLOR=0x00FF00      ; Green transmission flash
    -DNEW_MSG_LED=0x0000FF       ; Blue message breathing
```

### In Code
```cpp
#define TX_LED_COLOR 0x00FF00    // Green transmission
#define NEW_MSG_LED 0x0000FF     // Blue messages
```

## 🌈 Popular Colors

| Color | Hex | Use For |
|-------|-----|---------|
| Blue | `0x0000FF` | Professional, calm |
| Green | `0x00FF00` | Success, go, battery-friendly |
| Red | `0xFF0000` | Urgent, stop, attention |
| Yellow | `0xFFFF00` | Warning, caution |
| Cyan | `0x00FFFF` | Modern, cool |
| Magenta | `0xFF00FF` | Unique, stands out |
| Orange | `0xFF8000` | Friendly, warm |
| Purple | `0x8000FF` | Subtle, creative |
| White | `0xFFFFFF` | Maximum visibility |

## 📋 Recommended Combinations

### Defaults
```ini
-DTX_LED_COLOR=0x009600 -DNEW_MSG_LED=0x0000FF
```
Green TX flash (dimmed), Blue message breathing

### All Green (Battery Saver)
```ini
-DTX_LED_COLOR=0x00FF00 -DNEW_MSG_LED=0x00FF00
```

### All Red (High Urgency)
```ini
-DTX_LED_COLOR=0xFF0000 -DNEW_MSG_LED=0xFF0000
```

### High Contrast
```ini
-DTX_LED_COLOR=0xFF0000 -DNEW_MSG_LED=0x0000FF
```
Red TX, Blue messages - easy to distinguish

### Professional
```ini
-DTX_LED_COLOR=0x0000FF -DNEW_MSG_LED=0x00FFFF
```
Blue TX, Cyan messages - business-friendly

### Stealth Mode
```ini
-DTX_LED_COLOR=0x202020 -DNEW_MSG_LED=0x404040
```
Very dim for low visibility

## 🔧 Files Modified

1. **BanditBoard.h** - TX_LED_COLOR support
2. **UITask.h** - NEW_MSG_LED variables
3. **UITask.cpp** - NEW_MSG_LED color system

## 📊 LED Behavior

| Event | NeoPixels 0-1 | NeoPixels 2-5 |
|-------|---------------|---------------|
| Radio TX | Unchanged | TX_LED_COLOR (flash) |
| New messages | Unchanged | NEW_MSG_LED (breathing) |
| No messages | Unchanged | OFF |

## 🎛️ Advanced Tweaks

### Dim TX Flash
```cpp
#define TX_LED_COLOR 0x004000  // Dim green (was 0x00FF00)
```

### Brighter Messages
```cpp
#define NEOPIXEL_MAX_BRIGHTNESS 120  // In UITask.cpp (was 80)
```

### Faster Breathing
```cpp
#define NEOPIXEL_MSG_UPDATE_MILLIS 20  // In UITask.cpp (was 30)
```

### Smoother Breathing
```cpp
#define NEOPIXEL_BRIGHTNESS_STEP 1  // In UITask.cpp (was 2)
```

## 🔋 Battery Considerations

**Most Efficient (single LED):**
- `0xFF0000` (Red only)
- `0x00FF00` (Green only)
- `0x0000FF` (Blue only)

**Less Efficient (multiple LEDs):**
- `0xFFFF00` (Yellow = Red + Green)
- `0x00FFFF` (Cyan = Green + Blue)
- `0xFF00FF` (Magenta = Red + Blue)
- `0xFFFFFF` (White = all three)

**Brightness matters:**
- `0x00FF00` = full brightness
- `0x008000` = half brightness (50% power)
- `0x004000` = quarter brightness (25% power)

## 💡 Color Format

`0xRRGGBB` where:
- RR = Red (00-FF hex)
- GG = Green (00-FF hex)
- BB = Blue (00-FF hex)

Examples:
- Orange: `0xFF8000` = RGB(255, 128, 0)
- Pink: `0xFF69B4` = RGB(255, 105, 180)
- Teal: `0x008080` = RGB(0, 128, 128)

## 🧪 Testing

1. Set colors in platformio.ini
2. Build: `pio run`
3. Flash: `pio run -t upload`
4. Test TX: Send a message → see TX_LED_COLOR flash
5. Test MSG: Have unread messages → see NEW_MSG_LED breathing
6. Adjust as needed

## ⚠️ Troubleshooting

**Wrong color?**
→ Check hex format: `0xRRGGBB` with `0x` prefix

**TX too bright?**
→ Lower TX_LED_COLOR values: `0x00FF00` → `0x008000`

**Messages too dim?**
→ Increase NEOPIXEL_MAX_BRIGHTNESS in UITask.cpp

**Colors not applying?**
→ Check compiler output for build flags

## 📝 Quick Copy Examples

### Green TX, Blue MSG (default)
```
-DTX_LED_COLOR=0x009600 -DNEW_MSG_LED=0x0000FF
```

### Red TX, Red MSG
```
-DTX_LED_COLOR=0xFF0000 -DNEW_MSG_LED=0xFF0000
```

### Yellow TX, Magenta MSG
```
-DTX_LED_COLOR=0xFFFF00 -DNEW_MSG_LED=0xFF00FF
```

### Cyan TX, Purple MSG
```
-DTX_LED_COLOR=0x00FFFF -DNEW_MSG_LED=0x8000FF
```

### Dim White TX, Bright White MSG
```
-DTX_LED_COLOR=0x404040 -DNEW_MSG_LED=0xFFFFFF
```
