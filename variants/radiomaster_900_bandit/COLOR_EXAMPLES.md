# NeoPixel Color Examples & Recommendations

## 🎨 Complete Color Palette

### Primary Colors
```
Red:     0xFF0000  🔴  High urgency, attention-grabbing
Green:   0x00FF00  🟢  Success, go, nature
Blue:    0x0000FF  🔵  Calm, default, trust (DEFAULT)
```

### Secondary Colors
```
Yellow:  0xFFFF00  🟡  Warning, caution, bright
Cyan:    0x00FFFF  ⚡  Cool, techy, calming
Magenta: 0xFF00FF  💜  Unique, stands out
```

### Extended Palette
```
Orange:      0xFF8000  🟠  Warm, friendly
Purple:      0x8000FF  🟣  Creative, gentle
Pink:        0xFF1493  💗  Soft, playful
Hot Pink:    0xFF69B4  🎀  Very noticeable
Lime:        0xBFFF00  🟢  Fresh, energetic
Teal:        0x008080  🌊  Sophisticated
Navy:        0x000080  🌑  Subtle, professional
Maroon:      0x800000  🍷  Rich, subdued
Olive:       0x808000  🫒  Natural, earthy
Aqua:        0x00FF88  💎  Tropical, bright
Violet:      0x8800FF  🔮  Mystical
Coral:       0xFF7F50  🐠  Warm, inviting
Turquoise:   0x40E0D0  🏖️  Refreshing
Gold:        0xFFD700  ⭐  Valuable, special
Silver:      0xC0C0C0  🤖  Modern, sleek
```

### White Variants
```
Pure White:  0xFFFFFF  ⚪  Maximum brightness
Warm White:  0xFFCC88  🕯️  Cozy, comfortable
Cool White:  0xCCDDFF  ❄️  Clinical, clear
Soft White:  0xFFEEDD  🌟  Gentle, easy on eyes
```

## 📋 Use Case Recommendations

### High Priority / Urgent Messages
**Best choices:**
- `0xFF0000` (Red) - Classic alert color
- `0xFF00FF` (Magenta) - Unusual, catches attention
- `0xFFFF00` (Yellow) - Warning/caution
- `0xFF1493` (Hot Pink) - Very noticeable

**Settings for urgent:**
```ini
build_flags = 
    -DNEW_MSG_LED=0xFF0000              ; Red
    -DNEOPIXEL_MAX_BRIGHTNESS=120       ; Brighter
    -DNEOPIXEL_MSG_UPDATE_MILLIS=20     ; Faster breathing
```

### Normal Priority Messages
**Best choices:**
- `0x0000FF` (Blue) - Default, professional
- `0x00FFFF` (Cyan) - Visible but calm
- `0x8000FF` (Purple) - Distinctive yet gentle
- `0x00FF00` (Green) - Positive feeling

**Settings for normal:**
```ini
build_flags = 
    -DNEW_MSG_LED=0x0000FF              ; Blue
    -DNEOPIXEL_MAX_BRIGHTNESS=80        ; Standard
    -DNEOPIXEL_MSG_UPDATE_MILLIS=30     ; Normal speed
```

### Low Distraction / Ambient
**Best choices:**
- `0x8000FF` (Purple) - Subtle
- `0x008080` (Teal) - Sophisticated
- `0x40E0D0` (Turquoise) - Calming
- `0xFF7F50` (Coral) - Warm but not harsh

**Settings for ambient:**
```ini
build_flags = 
    -DNEW_MSG_LED=0x8000FF              ; Purple
    -DNEOPIXEL_MAX_BRIGHTNESS=40        ; Dim
    -DNEOPIXEL_MSG_UPDATE_MILLIS=50     ; Slow breathing
```

### Battery Saving
**Best choices (single LED color):**
- `0xFF0000` (Red) - Only red LED
- `0x00FF00` (Green) - Only green LED
- `0x0000FF` (Blue) - Only blue LED

**Avoid for battery:**
- ❌ `0xFFFFFF` (White) - All 3 LEDs at full
- ❌ `0xFFFF00` (Yellow) - 2 LEDs at full
- ❌ `0x00FFFF` (Cyan) - 2 LEDs at full
- ❌ `0xFF00FF` (Magenta) - 2 LEDs at full

**Battery-conscious settings:**
```ini
build_flags = 
    -DNEW_MSG_LED=0x00FF00              ; Green (single LED)
    -DNEOPIXEL_MAX_BRIGHTNESS=60        ; Moderate brightness
    -DNEOPIXEL_MIN_BRIGHTNESS=3         ; Very dim minimum
```

## 🎭 Themed Configurations

### "Stealth Mode" - Minimal Visibility
```ini
build_flags = 
    -DNEW_MSG_LED=0x000080              ; Navy blue
    -DNEOPIXEL_MAX_BRIGHTNESS=30
    -DNEOPIXEL_MIN_BRIGHTNESS=2
    -DNEOPIXEL_MSG_UPDATE_MILLIS=60
```

### "Party Mode" - Maximum Attention
```ini
build_flags = 
    -DNEW_MSG_LED=0xFF00FF              ; Magenta
    -DNEOPIXEL_MAX_BRIGHTNESS=150
    -DNEOPIXEL_MIN_BRIGHTNESS=10
    -DNEOPIXEL_MSG_UPDATE_MILLIS=15
```

### "Professional" - Subtle Business Setting
```ini
build_flags = 
    -DNEW_MSG_LED=0x0000FF              ; Blue
    -DNEOPIXEL_MAX_BRIGHTNESS=60
    -DNEOPIXEL_MSG_UPDATE_MILLIS=40
```

### "Emergency Services" - High Visibility Red
```ini
build_flags = 
    -DNEW_MSG_LED=0xFF0000              ; Red
    -DNEOPIXEL_MAX_BRIGHTNESS=120
    -DNEOPIXEL_MSG_UPDATE_MILLIS=15
```

### "Nature Lover" - Earth Tones
```ini
build_flags = 
    -DNEW_MSG_LED=0x00FF00              ; Green
    -DNEOPIXEL_MAX_BRIGHTNESS=70
    -DNEOPIXEL_MSG_UPDATE_MILLIS=35
```

### "Gamer RGB" - Vibrant Cyan
```ini
build_flags = 
    -DNEW_MSG_LED=0x00FFFF              ; Cyan
    -DNEOPIXEL_MAX_BRIGHTNESS=100
    -DNEOPIXEL_MSG_UPDATE_MILLIS=25
```

## 🔬 Color Mixing Guide

Want a custom color? Mix Red, Green, and Blue:

```
Formula: 0xRRGGBB

RR = Red intensity   (00 to FF in hex, 0 to 255 decimal)
GG = Green intensity (00 to FF in hex, 0 to 255 decimal)
BB = Blue intensity  (00 to FF in hex, 0 to 255 decimal)
```

### Quick Mix Examples:

**Orange** (Red + half Green):
```
0xFF8000 = 0xFF (255 red) + 0x80 (128 green) + 0x00 (0 blue)
```

**Purple** (Red + Blue):
```
0x8000FF = 0x80 (128 red) + 0x00 (0 green) + 0xFF (255 blue)
```

**Pink** (Red + some Green + Blue):
```
0xFF69B4 = 0xFF (255 red) + 0x69 (105 green) + 0xB4 (180 blue)
```

### Hex to Decimal Quick Reference:
```
00 = 0      40 = 64     80 = 128    C0 = 192
20 = 32     60 = 96     A0 = 160    E0 = 224
30 = 48     70 = 112    B0 = 176    FF = 255
```

## 💡 Pro Tips

### Visibility in Different Lighting
- **Daylight/Bright**: Use high brightness (100-150) and saturated colors
- **Indoor/Office**: Medium brightness (60-80) with any color
- **Dark/Night**: Low brightness (30-50) to avoid being blinding
- **Outdoors**: Blue/Cyan show well in daylight, red at night

### Color Psychology
- **Red**: Urgency, danger, stop, important
- **Green**: Go, success, safe, nature
- **Blue**: Trust, calm, professional, default
- **Yellow**: Warning, caution, attention
- **Purple**: Creative, unique, mystical
- **Cyan**: Tech, modern, cool
- **Orange**: Friendly, energetic, warm
- **White**: Clean, pure, maximum visibility

### Multi-User Scenarios
If multiple people use devices, assign colors:
- User 1: Blue `0x0000FF`
- User 2: Green `0x00FF00`
- User 3: Red `0xFF0000`
- User 4: Magenta `0xFF00FF`
- User 5: Cyan `0x00FFFF`

## 🧪 Testing Your Color

1. Choose your hex color
2. Add to platformio.ini: `-DNEW_MSG_LED=0xYOURCOLOR`
3. Build and flash
4. Send yourself a test message
5. Observe the breathing effect
6. Adjust brightness/speed if needed

## 📱 Web Color Picker

Use any online color picker (search "RGB color picker") to find your perfect color:
1. Pick your color visually
2. Copy the RGB hex value (usually shown as #RRGGBB)
3. Change # to 0x (e.g., #00FF00 becomes 0x00FF00)
4. Use in your build configuration

## 🎨 Seasonal Colors

### Spring
```
Pastel Pink:   0xFFB6C1
Mint Green:    0x98FF98
Lavender:      0xE6E6FA
```

### Summer
```
Tropical Blue: 0x00CED1
Bright Yellow: 0xFFFF00
Coral:         0xFF7F50
```

### Autumn
```
Burnt Orange:  0xFF8C00
Rust:          0xB7410E
Gold:          0xFFD700
```

### Winter
```
Ice Blue:      0xAFEEEE
Snow White:    0xFFFAFA
Silver:        0xC0C0C0
```

---

Remember: The color appears as a breathing/pulsing animation, so it will cycle between dim and bright, making it more dynamic than a static color!
