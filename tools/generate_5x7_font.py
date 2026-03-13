#!/usr/bin/env python3
"""
Generate a custom 5x7 bitmap font header (locale_font.h) that replaces CP437's
0x80+ range with Central/Eastern European national characters.

Usage:
    python3 tools/generate_5x7_font.py --preview
    python3 tools/generate_5x7_font.py --preview-image preview.png
    python3 tools/generate_5x7_font.py --output src/helpers/ui/locale_font.h

Glyphs are derived from the existing CP437 base letters by overlaying accent
marks — the same technique CP437 itself uses for characters like Ä, é, Ñ.
No TTF rendering is involved.

5x7 bitmap format: each char = 5 bytes (columns), bit 0 = top row, bit 6 =
bottom row, bit 7 = descender/spacing row. Column-major, left to right.
"""

import sys
import argparse

# ---------------------------------------------------------------------------
# CP437 font data from Adafruit GFX glcdfont.c (256 chars x 5 bytes)
# ---------------------------------------------------------------------------
CP437 = [
    0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x5B, 0x4F, 0x5B, 0x3E, 0x3E, 0x6B,
    0x4F, 0x6B, 0x3E, 0x1C, 0x3E, 0x7C, 0x3E, 0x1C, 0x18, 0x3C, 0x7E, 0x3C,
    0x18, 0x1C, 0x57, 0x7D, 0x57, 0x1C, 0x1C, 0x5E, 0x7F, 0x5E, 0x1C, 0x00,
    0x18, 0x3C, 0x18, 0x00, 0xFF, 0xE7, 0xC3, 0xE7, 0xFF, 0x00, 0x18, 0x24,
    0x18, 0x00, 0xFF, 0xE7, 0xDB, 0xE7, 0xFF, 0x30, 0x48, 0x3A, 0x06, 0x0E,
    0x26, 0x29, 0x79, 0x29, 0x26, 0x40, 0x7F, 0x05, 0x05, 0x07, 0x40, 0x7F,
    0x05, 0x25, 0x3F, 0x5A, 0x3C, 0xE7, 0x3C, 0x5A, 0x7F, 0x3E, 0x1C, 0x1C,
    0x08, 0x08, 0x1C, 0x1C, 0x3E, 0x7F, 0x14, 0x22, 0x7F, 0x22, 0x14, 0x5F,
    0x5F, 0x00, 0x5F, 0x5F, 0x06, 0x09, 0x7F, 0x01, 0x7F, 0x00, 0x66, 0x89,
    0x95, 0x6A, 0x60, 0x60, 0x60, 0x60, 0x60, 0x94, 0xA2, 0xFF, 0xA2, 0x94,
    0x08, 0x04, 0x7E, 0x04, 0x08, 0x10, 0x20, 0x7E, 0x20, 0x10, 0x08, 0x08,
    0x2A, 0x1C, 0x08, 0x08, 0x1C, 0x2A, 0x08, 0x08, 0x1E, 0x10, 0x10, 0x10,
    0x10, 0x0C, 0x1E, 0x0C, 0x1E, 0x0C, 0x30, 0x38, 0x3E, 0x38, 0x30, 0x06,
    0x0E, 0x3E, 0x0E, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F,
    0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x14, 0x7F, 0x14, 0x7F, 0x14,
    0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x23, 0x13, 0x08, 0x64, 0x62, 0x36, 0x49,
    0x56, 0x20, 0x50, 0x00, 0x08, 0x07, 0x03, 0x00, 0x00, 0x1C, 0x22, 0x41,
    0x00, 0x00, 0x41, 0x22, 0x1C, 0x00, 0x2A, 0x1C, 0x7F, 0x1C, 0x2A, 0x08,
    0x08, 0x3E, 0x08, 0x08, 0x00, 0x80, 0x70, 0x30, 0x00, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x00, 0x00, 0x60, 0x60, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02,
    0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x72, 0x49,
    0x49, 0x49, 0x46, 0x21, 0x41, 0x49, 0x4D, 0x33, 0x18, 0x14, 0x12, 0x7F,
    0x10, 0x27, 0x45, 0x45, 0x45, 0x39, 0x3C, 0x4A, 0x49, 0x49, 0x31, 0x41,
    0x21, 0x11, 0x09, 0x07, 0x36, 0x49, 0x49, 0x49, 0x36, 0x46, 0x49, 0x49,
    0x29, 0x1E, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x40, 0x34, 0x00, 0x00,
    0x00, 0x08, 0x14, 0x22, 0x41, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x41,
    0x22, 0x14, 0x08, 0x02, 0x01, 0x59, 0x09, 0x06, 0x3E, 0x41, 0x5D, 0x59,
    0x4E, 0x7C, 0x12, 0x11, 0x12, 0x7C, 0x7F, 0x49, 0x49, 0x49, 0x36, 0x3E,
    0x41, 0x41, 0x41, 0x22, 0x7F, 0x41, 0x41, 0x41, 0x3E, 0x7F, 0x49, 0x49,
    0x49, 0x41, 0x7F, 0x09, 0x09, 0x09, 0x01, 0x3E, 0x41, 0x41, 0x51, 0x73,
    0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x41, 0x7F, 0x41, 0x00, 0x20, 0x40,
    0x41, 0x3F, 0x01, 0x7F, 0x08, 0x14, 0x22, 0x41, 0x7F, 0x40, 0x40, 0x40,
    0x40, 0x7F, 0x02, 0x1C, 0x02, 0x7F, 0x7F, 0x04, 0x08, 0x10, 0x7F, 0x3E,
    0x41, 0x41, 0x41, 0x3E, 0x7F, 0x09, 0x09, 0x09, 0x06, 0x3E, 0x41, 0x51,
    0x21, 0x5E, 0x7F, 0x09, 0x19, 0x29, 0x46, 0x26, 0x49, 0x49, 0x49, 0x32,
    0x03, 0x01, 0x7F, 0x01, 0x03, 0x3F, 0x40, 0x40, 0x40, 0x3F, 0x1F, 0x20,
    0x40, 0x20, 0x1F, 0x3F, 0x40, 0x38, 0x40, 0x3F, 0x63, 0x14, 0x08, 0x14,
    0x63, 0x03, 0x04, 0x78, 0x04, 0x03, 0x61, 0x59, 0x49, 0x4D, 0x43, 0x00,
    0x7F, 0x41, 0x41, 0x41, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x41, 0x41,
    0x41, 0x7F, 0x04, 0x02, 0x01, 0x02, 0x04, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x00, 0x03, 0x07, 0x08, 0x00, 0x20, 0x54, 0x54, 0x78, 0x40, 0x7F, 0x28,
    0x44, 0x44, 0x38, 0x38, 0x44, 0x44, 0x44, 0x28, 0x38, 0x44, 0x44, 0x28,
    0x7F, 0x38, 0x54, 0x54, 0x54, 0x18, 0x00, 0x08, 0x7E, 0x09, 0x02, 0x18,
    0xA4, 0xA4, 0x9C, 0x78, 0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, 0x44, 0x7D,
    0x40, 0x00, 0x20, 0x40, 0x40, 0x3D, 0x00, 0x7F, 0x10, 0x28, 0x44, 0x00,
    0x00, 0x41, 0x7F, 0x40, 0x00, 0x7C, 0x04, 0x78, 0x04, 0x78, 0x7C, 0x08,
    0x04, 0x04, 0x78, 0x38, 0x44, 0x44, 0x44, 0x38, 0xFC, 0x18, 0x24, 0x24,
    0x18, 0x18, 0x24, 0x24, 0x18, 0xFC, 0x7C, 0x08, 0x04, 0x04, 0x08, 0x48,
    0x54, 0x54, 0x54, 0x24, 0x04, 0x04, 0x3F, 0x44, 0x24, 0x3C, 0x40, 0x40,
    0x20, 0x7C, 0x1C, 0x20, 0x40, 0x20, 0x1C, 0x3C, 0x40, 0x30, 0x40, 0x3C,
    0x44, 0x28, 0x10, 0x28, 0x44, 0x4C, 0x90, 0x90, 0x90, 0x7C, 0x44, 0x64,
    0x54, 0x4C, 0x44, 0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x00, 0x77, 0x00,
    0x00, 0x00, 0x41, 0x36, 0x08, 0x00, 0x02, 0x01, 0x02, 0x04, 0x02, 0x3C,
    0x26, 0x23, 0x26, 0x3C,
    # 0x80+ (CP437 extended)
    0x1E, 0xA1, 0xA1, 0x61, 0x12, 0x3A, 0x40, 0x40,
    0x20, 0x7A, 0x38, 0x54, 0x54, 0x55, 0x59, 0x21, 0x55, 0x55, 0x79, 0x41,
    0x22, 0x54, 0x54, 0x78, 0x42, 0x21, 0x55, 0x54, 0x78, 0x40, 0x20, 0x54,
    0x55, 0x79, 0x40, 0x0C, 0x1E, 0x52, 0x72, 0x12, 0x39, 0x55, 0x55, 0x55,
    0x59, 0x39, 0x54, 0x54, 0x54, 0x59, 0x39, 0x55, 0x54, 0x54, 0x58, 0x00,
    0x00, 0x45, 0x7C, 0x41, 0x00, 0x02, 0x45, 0x7D, 0x42, 0x00, 0x01, 0x45,
    0x7C, 0x40, 0x7D, 0x12, 0x11, 0x12, 0x7D, 0xF0, 0x28, 0x25, 0x28, 0xF0,
    0x7C, 0x54, 0x55, 0x45, 0x00, 0x20, 0x54, 0x54, 0x7C, 0x54, 0x7C, 0x0A,
    0x09, 0x7F, 0x49, 0x32, 0x49, 0x49, 0x49, 0x32, 0x3A, 0x44, 0x44, 0x44,
    0x3A, 0x32, 0x4A, 0x48, 0x48, 0x30, 0x3A, 0x41, 0x41, 0x21, 0x7A, 0x3A,
    0x42, 0x40, 0x20, 0x78, 0x00, 0x9D, 0xA0, 0xA0, 0x7D, 0x3D, 0x42, 0x42,
    0x42, 0x3D, 0x3D, 0x40, 0x40, 0x40, 0x3D, 0x3C, 0x24, 0xFF, 0x24, 0x24,
    0x48, 0x7E, 0x49, 0x43, 0x66, 0x2B, 0x2F, 0xFC, 0x2F, 0x2B, 0xFF, 0x09,
    0x29, 0xF6, 0x20, 0xC0, 0x88, 0x7E, 0x09, 0x03, 0x20, 0x54, 0x54, 0x79,
    0x41, 0x00, 0x00, 0x44, 0x7D, 0x41, 0x30, 0x48, 0x48, 0x4A, 0x32, 0x38,
    0x40, 0x40, 0x22, 0x7A, 0x00, 0x7A, 0x0A, 0x0A, 0x72, 0x7D, 0x0D, 0x19,
    0x31, 0x7D, 0x26, 0x29, 0x29, 0x2F, 0x28, 0x26, 0x29, 0x29, 0x29, 0x26,
    0x30, 0x48, 0x4D, 0x40, 0x20, 0x38, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x38, 0x2F, 0x10, 0xC8, 0xAC, 0xBA, 0x2F, 0x10, 0x28, 0x34,
    0xFA, 0x00, 0x00, 0x7B, 0x00, 0x00, 0x08, 0x14, 0x2A, 0x14, 0x22, 0x22,
    0x14, 0x2A, 0x14, 0x08, 0x55, 0x00, 0x55, 0x00, 0x55,
    0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0xFF, 0x55, 0xFF, 0x55, 0xFF,
    0x00, 0x00, 0x00, 0xFF, 0x00, 0x10, 0x10, 0x10, 0xFF, 0x00, 0x14, 0x14,
    0x14, 0xFF, 0x00, 0x10, 0x10, 0xFF, 0x00, 0xFF, 0x10, 0x10, 0xF0, 0x10,
    0xF0, 0x14, 0x14, 0x14, 0xFC, 0x00, 0x14, 0x14, 0xF7, 0x00, 0xFF, 0x00,
    0x00, 0xFF, 0x00, 0xFF, 0x14, 0x14, 0xF4, 0x04, 0xFC, 0x14, 0x14, 0x17,
    0x10, 0x1F, 0x10, 0x10, 0x1F, 0x10, 0x1F, 0x14, 0x14, 0x14, 0x1F, 0x00,
    0x10, 0x10, 0x10, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x10, 0x10, 0x10,
    0x10, 0x1F, 0x10, 0x10, 0x10, 0x10, 0xF0, 0x10, 0x00, 0x00, 0x00, 0xFF,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0xFF, 0x10, 0x00,
    0x00, 0x00, 0xFF, 0x14, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0x1F,
    0x10, 0x17, 0x00, 0x00, 0xFC, 0x04, 0xF4, 0x14, 0x14, 0x17, 0x10, 0x17,
    0x14, 0x14, 0xF4, 0x04, 0xF4, 0x00, 0x00, 0xFF, 0x00, 0xF7, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0xF7, 0x00, 0xF7, 0x14, 0x14, 0x14, 0x17,
    0x14, 0x10, 0x10, 0x1F, 0x10, 0x1F, 0x14, 0x14, 0x14, 0xF4, 0x14, 0x10,
    0x10, 0xF0, 0x10, 0xF0, 0x00, 0x00, 0x1F, 0x10, 0x1F, 0x00, 0x00, 0x00,
    0x1F, 0x14, 0x00, 0x00, 0x00, 0xFC, 0x14, 0x00, 0x00, 0xF0, 0x10, 0xF0,
    0x10, 0x10, 0xFF, 0x10, 0xFF, 0x14, 0x14, 0x14, 0xFF, 0x14, 0x10, 0x10,
    0x10, 0x1F, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x10, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x38, 0x44, 0x44,
    0x38, 0x44, 0xFC, 0x4A, 0x4A, 0x4A, 0x34,
    0x7E, 0x02, 0x02, 0x06, 0x06, 0x02, 0x7E, 0x02, 0x7E, 0x02, 0x63, 0x55,
    0x49, 0x41, 0x63, 0x38, 0x44, 0x44, 0x3C, 0x04, 0x40, 0x7E, 0x20, 0x1E,
    0x20, 0x06, 0x02, 0x7E, 0x02, 0x02, 0x99, 0xA5, 0xE7, 0xA5, 0x99, 0x1C,
    0x2A, 0x49, 0x2A, 0x1C, 0x4C, 0x72, 0x01, 0x72, 0x4C, 0x30, 0x4A, 0x4D,
    0x4D, 0x30, 0x30, 0x48, 0x78, 0x48, 0x30, 0xBC, 0x62, 0x5A, 0x46, 0x3D,
    0x3E, 0x49, 0x49, 0x49, 0x00, 0x7E, 0x01, 0x01, 0x01, 0x7E, 0x2A, 0x2A,
    0x2A, 0x2A, 0x2A, 0x44, 0x44, 0x5F, 0x44, 0x44, 0x40, 0x51, 0x4A, 0x44,
    0x40, 0x40, 0x44, 0x4A, 0x51, 0x40, 0x00, 0x00, 0xFF, 0x01, 0x03, 0xE0,
    0x80, 0xFF, 0x00, 0x00, 0x08, 0x08, 0x6B, 0x6B, 0x08, 0x36, 0x12, 0x36,
    0x24, 0x36, 0x06, 0x0F, 0x09, 0x0F, 0x06, 0x00, 0x00, 0x18, 0x18, 0x00,
    0x00, 0x00, 0x10, 0x10, 0x00, 0x30, 0x40, 0xFF, 0x01, 0x01, 0x00, 0x1F,
    0x01, 0x01, 0x1E, 0x00, 0x19, 0x1D, 0x17, 0x12, 0x00, 0x3C, 0x3C, 0x3C,
    0x3C, 0x00, 0x00, 0x00, 0x00, 0x00,
]


# ---------------------------------------------------------------------------
# Glyph construction helpers
# ---------------------------------------------------------------------------

def get_glyph(char_code):
    """Get 5 bytes for a CP437 character by its code point (0x00-0xFF)."""
    return list(CP437[char_code * 5 : char_code * 5 + 5])


def set_pixel(glyph, col, row):
    """Set a pixel in a 5-byte glyph. col=0..4, row=0..7."""
    glyph[col] |= (1 << row)


def clear_pixel(glyph, col, row):
    """Clear a pixel in a 5-byte glyph."""
    glyph[col] &= ~(1 << row)


def shift_down(glyph, n=1):
    """Shift all pixels down by n rows (lose bottom pixels that shift past row 7)."""
    return [(b << n) & 0xFF for b in glyph]


def has_pixel(glyph, col, row):
    """Check if pixel is set."""
    return bool(glyph[col] & (1 << row))


def accent_fits(glyph, positions):
    """Check if accent mark positions don't conflict with existing letter pixels."""
    for col, row in positions:
        if has_pixel(glyph, col, row):
            return False
    return True


def apply_accent_above(base_code, accent_positions):
    """Create accented glyph. If accent conflicts with letter, shift letter down."""
    g = get_glyph(base_code)
    if not accent_fits(g, accent_positions):
        g = shift_down(g)
    for col, row in accent_positions:
        set_pixel(g, col, row)
    return g


def apply_below(base_code, positions):
    """Add mark below letter (ogonek/cedilla at row 7)."""
    g = get_glyph(base_code)
    for col, row in positions:
        set_pixel(g, col, row)
    return g


# ---------------------------------------------------------------------------
# Accent patterns (derived from studying CP437's own accented characters)
#
# CP437 reference:
#   é = e + {(3,0),(4,0)}           acute: 2 pixels upper-right
#   è = e + {(0,0),(1,0)}           grave: 2 pixels upper-left
#   ê = e + all 5 cols at row 0     circumflex: full bar
#   ä = a + {(0,1),(4,1)}           umlaut: dots at edges, row 1
#   Ä = A + {(0,0),(4,0)}           UC umlaut: dots at edges, row 0
#   Å = A shifted down + {(2,0)}    ring: center dot, letter shifted
# ---------------------------------------------------------------------------

# Lowercase accents (rows 0-1 are free for most lowercase letters)
ACUTE_LC    = [(3, 0), (4, 0)]              # ´  from CP437 é pattern
CARON_LC    = [(1, 0), (3, 0), (2, 1)]      # ˇ  V-shape across rows 0-1
DOT_LC      = [(2, 0)]                      # ˙  single center pixel
BREVE_LC    = [(1, 0), (2, 1), (3, 0)]      # ˘  same shape as caron (indistinguishable at 5px)
MACRON_LC   = [(1, 0), (2, 0), (3, 0)]      # ¯  horizontal bar
RING_LC     = [(1, 0), (3, 0), (2, 1)]      # ˚  tiny ring ≈ caron at this res
DBLACUTE_LC = [(1, 0), (2, 0), (3, 0), (4, 0)]  # ˝  wider bar to distinguish from acute

# Uppercase accents (row 0 only; letter shifted down if conflict)
ACUTE_UC    = [(3, 0), (4, 0)]              # ´
CARON_UC    = [(1, 0), (3, 0)]              # ˇ  two dots (letter peak at col2 row1 completes V)
DOT_UC      = [(2, 0)]                      # ˙
BREVE_UC    = [(1, 0), (3, 0)]              # ˘
MACRON_UC   = [(1, 0), (2, 0), (3, 0)]      # ¯
RING_UC     = [(2, 0)]                      # ˚  center dot (like CP437 Å)
DBLACUTE_UC = [(1, 0), (2, 0), (3, 0), (4, 0)]  # ˝

# Below-letter marks (row 7)
OGONEK  = [(3, 7), (4, 7)]                  # ˛  two pixels lower-right
CEDILLA = [(2, 7), (3, 7)]                  # ¸  two pixels lower-center


# ---------------------------------------------------------------------------
# Build all 80 CE glyphs from base letters + accent overlays
# ---------------------------------------------------------------------------

def build_ce_glyphs():
    """Build all CE national character glyphs.

    Returns dict: slot -> (5-byte glyph, unicode_char, description, base_char)
    """
    glyphs = {}

    def add(slot, char, desc, glyph, base='?'):
        glyphs[slot] = (glyph, char, desc, base)

    # --- Polish (16) ---
    add(0x81, '\u0104', 'Aogonek',  apply_below(ord('A'), OGONEK), 'A')
    add(0x82, '\u0105', 'aogonek',  apply_below(ord('a'), OGONEK), 'a')
    add(0x83, '\u0106', 'Cacute',   apply_accent_above(ord('C'), ACUTE_UC), 'C')
    add(0x84, '\u0107', 'cacute',   apply_accent_above(ord('c'), ACUTE_LC), 'c')
    add(0x85, '\u0118', 'Eogonek',  apply_below(ord('E'), OGONEK), 'E')
    add(0x86, '\u0119', 'eogonek',  apply_below(ord('e'), OGONEK), 'e')

    # Ł: L with stroke - add horizontal bar through stem
    g = get_glyph(ord('L'))
    set_pixel(g, 1, 2); set_pixel(g, 1, 3)  # bar extending right from stem
    add(0x87, '\u0141', 'Lstroke', g, 'L')

    # ł: l with stroke - add bar through stem
    g = get_glyph(ord('l'))
    set_pixel(g, 1, 2); set_pixel(g, 3, 3)  # diagonal stroke
    add(0x88, '\u0142', 'lstroke', g, 'l')

    add(0x89, '\u0143', 'Nacute',  apply_accent_above(ord('N'), ACUTE_UC), 'N')
    add(0x8A, '\u0144', 'nacute',  apply_accent_above(ord('n'), ACUTE_LC), 'n')
    add(0x8B, '\u015A', 'Sacute',  apply_accent_above(ord('S'), ACUTE_UC), 'S')
    add(0x8C, '\u015B', 'sacute',  apply_accent_above(ord('s'), ACUTE_LC), 's')
    add(0x8D, '\u0179', 'Zacute',  apply_accent_above(ord('Z'), ACUTE_UC), 'Z')
    add(0x8E, '\u017A', 'zacute',  apply_accent_above(ord('z'), ACUTE_LC), 'z')
    add(0x8F, '\u017B', 'Zdot',    apply_accent_above(ord('Z'), DOT_UC), 'Z')
    add(0x90, '\u017C', 'zdot',    apply_accent_above(ord('z'), DOT_LC), 'z')

    # --- Czech/Slovak (24) ---
    add(0x91, '\u010C', 'Ccaron',  apply_accent_above(ord('C'), CARON_UC), 'C')
    add(0x92, '\u010D', 'ccaron',  apply_accent_above(ord('c'), CARON_LC), 'c')
    add(0x93, '\u010E', 'Dcaron',  apply_accent_above(ord('D'), CARON_UC), 'D')
    add(0x94, '\u010F', 'dcaron',  apply_accent_above(ord('d'), CARON_LC), 'd')
    add(0x95, '\u011A', 'Ecaron',  apply_accent_above(ord('E'), CARON_UC), 'E')
    add(0x96, '\u011B', 'ecaron',  apply_accent_above(ord('e'), CARON_LC), 'e')
    add(0x97, '\u0139', 'Lacute',  apply_accent_above(ord('L'), ACUTE_UC), 'L')
    add(0x98, '\u013A', 'lacute',  apply_accent_above(ord('l'), ACUTE_LC), 'l')
    add(0x99, '\u013D', 'Lcaron',  apply_accent_above(ord('L'), CARON_UC), 'L')
    add(0x9A, '\u013E', 'lcaron',  apply_accent_above(ord('l'), CARON_LC), 'l')
    add(0x9B, '\u0147', 'Ncaron',  apply_accent_above(ord('N'), CARON_UC), 'N')
    add(0x9C, '\u0148', 'ncaron',  apply_accent_above(ord('n'), CARON_LC), 'n')
    add(0x9D, '\u0154', 'Racute',  apply_accent_above(ord('R'), ACUTE_UC), 'R')
    add(0x9E, '\u0155', 'racute',  apply_accent_above(ord('r'), ACUTE_LC), 'r')
    add(0x9F, '\u0158', 'Rcaron',  apply_accent_above(ord('R'), CARON_UC), 'R')
    add(0xA0, '\u0159', 'rcaron',  apply_accent_above(ord('r'), CARON_LC), 'r')
    add(0xA1, '\u0160', 'Scaron',  apply_accent_above(ord('S'), CARON_UC), 'S')
    add(0xA2, '\u0161', 'scaron',  apply_accent_above(ord('s'), CARON_LC), 's')
    add(0xA3, '\u0164', 'Tcaron',  apply_accent_above(ord('T'), CARON_UC), 'T')
    add(0xA4, '\u0165', 'tcaron',  apply_accent_above(ord('t'), CARON_LC), 't')
    add(0xA5, '\u016E', 'Uring',   apply_accent_above(ord('U'), RING_UC), 'U')
    add(0xA6, '\u016F', 'uring',   apply_accent_above(ord('u'), RING_LC), 'u')
    add(0xA7, '\u017D', 'Zcaron',  apply_accent_above(ord('Z'), CARON_UC), 'Z')
    add(0xA8, '\u017E', 'zcaron',  apply_accent_above(ord('z'), CARON_LC), 'z')

    # --- Hungarian (4) ---
    add(0xA9, '\u0150', 'Odblacute', apply_accent_above(ord('O'), DBLACUTE_UC), 'O')
    add(0xAA, '\u0151', 'odblacute', apply_accent_above(ord('o'), DBLACUTE_LC), 'o')
    add(0xAB, '\u0170', 'Udblacute', apply_accent_above(ord('U'), DBLACUTE_UC), 'U')
    add(0xAC, '\u0171', 'udblacute', apply_accent_above(ord('u'), DBLACUTE_LC), 'u')

    # --- Romanian (6) ---
    add(0xAD, '\u0102', 'Abreve',  apply_accent_above(ord('A'), BREVE_UC), 'A')
    add(0xAE, '\u0103', 'abreve',  apply_accent_above(ord('a'), BREVE_LC), 'a')
    add(0xAF, '\u0218', 'Scomma',  apply_below(ord('S'), CEDILLA), 'S')
    add(0xB0, '\u0219', 'scomma',  apply_below(ord('s'), CEDILLA), 's')
    add(0xB1, '\u021A', 'Tcomma',  apply_below(ord('T'), CEDILLA), 'T')
    add(0xB2, '\u021B', 'tcomma',  apply_below(ord('t'), CEDILLA), 't')

    # --- Croatian (2 unique) ---
    # Đ: D with stroke - horizontal bar through middle
    g = get_glyph(ord('D'))
    set_pixel(g, 0, 3)  # extend bar left (col0 already has row3 from D stem)
    # D's col1 at row3 is clear, so add the bar pixel there
    set_pixel(g, 1, 3)
    add(0xB3, '\u0110', 'Dstroke', g, 'D')

    # đ: d with stroke through ascender
    g = get_glyph(ord('d'))
    set_pixel(g, 3, 1)  # bar extending left from ascender area
    add(0xB4, '\u0111', 'dstroke', g, 'd')

    # --- Turkish (6) ---
    add(0xB5, '\u011E', 'Gbreve',    apply_accent_above(ord('G'), BREVE_UC), 'G')
    add(0xB6, '\u011F', 'gbreve',    apply_accent_above(ord('g'), BREVE_LC), 'g')

    # İ: I with dot above (Turkish capital I has a dot)
    add(0xB7, '\u0130', 'Idot',      apply_accent_above(ord('I'), DOT_UC), 'I')

    # ı: dotless i
    g = get_glyph(ord('i'))
    clear_pixel(g, 2, 0)  # remove dot from 'i'
    # 'i' in CP437 is: 0x00, 0x44, 0x7D, 0x40, 0x00 - dot is bit0 of col2 (0x7D -> 0x7C)
    add(0xB8, '\u0131', 'idotless', g, 'i')

    add(0xB9, '\u015E', 'Scedilla',  apply_below(ord('S'), CEDILLA), 'S')
    add(0xBA, '\u015F', 'scedilla',  apply_below(ord('s'), CEDILLA), 's')

    # --- Lithuanian (8 unique) ---
    add(0xBB, '\u0116', 'Edot',     apply_accent_above(ord('E'), DOT_UC), 'E')
    add(0xBC, '\u0117', 'edot',     apply_accent_above(ord('e'), DOT_LC), 'e')
    add(0xBD, '\u012E', 'Iogonek',  apply_below(ord('I'), OGONEK), 'I')
    add(0xBE, '\u012F', 'iogonek',  apply_below(ord('i'), OGONEK), 'i')
    add(0xBF, '\u016A', 'Umacron',  apply_accent_above(ord('U'), MACRON_UC), 'U')
    add(0xC0, '\u016B', 'umacron',  apply_accent_above(ord('u'), MACRON_LC), 'u')
    add(0xC1, '\u0172', 'Uogonek',  apply_below(ord('U'), OGONEK), 'U')
    add(0xC2, '\u0173', 'uogonek',  apply_below(ord('u'), OGONEK), 'u')

    # --- Latvian (14 unique) ---
    add(0xC3, '\u0100', 'Amacron',   apply_accent_above(ord('A'), MACRON_UC), 'A')
    add(0xC4, '\u0101', 'amacron',   apply_accent_above(ord('a'), MACRON_LC), 'a')
    add(0xC5, '\u0112', 'Emacron',   apply_accent_above(ord('E'), MACRON_UC), 'E')
    add(0xC6, '\u0113', 'emacron',   apply_accent_above(ord('e'), MACRON_LC), 'e')
    add(0xC7, '\u0122', 'Gcedilla',  apply_below(ord('G'), CEDILLA), 'G')
    add(0xC8, '\u0123', 'gcedilla',  apply_below(ord('g'), CEDILLA), 'g')
    add(0xC9, '\u012A', 'Imacron',   apply_accent_above(ord('I'), MACRON_UC), 'I')
    add(0xCA, '\u012B', 'imacron',   apply_accent_above(ord('i'), MACRON_LC), 'i')
    add(0xCB, '\u0136', 'Kcedilla',  apply_below(ord('K'), CEDILLA), 'K')
    add(0xCC, '\u0137', 'kcedilla',  apply_below(ord('k'), CEDILLA), 'k')
    add(0xCD, '\u013B', 'Lcedilla',  apply_below(ord('L'), CEDILLA), 'L')
    add(0xCE, '\u013C', 'lcedilla',  apply_below(ord('l'), CEDILLA), 'l')
    add(0xCF, '\u0145', 'Ncedilla',  apply_below(ord('N'), CEDILLA), 'N')
    add(0xD0, '\u0146', 'ncedilla',  apply_below(ord('n'), CEDILLA), 'n')

    # --- Common Latin-1 accented chars used across CE languages ---
    # Lowercase: copy proven bitmaps from CP437 original positions
    # CP437 positions: á=0xA0, é=0x82, í=0xA1, ó=0xA2, ú=0xA3, ö=0x94, ü=0x81,
    #                  ô=0x93, â=0x83, î=0x8C, ä=0x84
    def cp437(pos):
        """Get glyph from original CP437 position."""
        return list(CP437[pos * 5 : pos * 5 + 5])

    add(0xD1, '\u00E1', 'aacute',  cp437(0xA0), 'a')
    add(0xD2, '\u00E9', 'eacute',  cp437(0x82), 'e')
    add(0xD3, '\u00ED', 'iacute',  cp437(0xA1), 'i')
    add(0xD4, '\u00F3', 'oacute',  cp437(0xA2), 'o')
    add(0xD5, '\u00FA', 'uacute',  cp437(0xA3), 'u')
    add(0xD6, '\u00FD', 'yacute',  apply_accent_above(ord('y'), ACUTE_LC), 'y')
    add(0xD7, '\u00F6', 'odiaeresis', cp437(0x94), 'o')
    add(0xD8, '\u00FC', 'udiaeresis', cp437(0x81), 'u')
    add(0xD9, '\u00F4', 'ocircumflex', cp437(0x93), 'o')
    add(0xDA, '\u00E2', 'acircumflex', cp437(0x83), 'a')
    add(0xDB, '\u00EE', 'icircumflex', cp437(0x8C), 'i')
    add(0xDC, '\u00E4', 'adiaeresis', cp437(0x84), 'a')

    # Uppercase: derive from base + accent (CP437 only has É at 0x90, Ä at 0x8E, Ö at 0x99, Ü at 0x9A)
    add(0xDD, '\u00C1', 'Aacute',  apply_accent_above(ord('A'), ACUTE_UC), 'A')
    add(0xDE, '\u00C9', 'Eacute',  cp437(0x90), 'E')  # CP437 has this
    add(0xDF, '\u00CD', 'Iacute',  apply_accent_above(ord('I'), ACUTE_UC), 'I')
    add(0xE0, '\u00D3', 'Oacute',  apply_accent_above(ord('O'), ACUTE_UC), 'O')
    add(0xE1, '\u00DA', 'Uacute',  apply_accent_above(ord('U'), ACUTE_UC), 'U')
    add(0xE2, '\u00DD', 'Yacute',  apply_accent_above(ord('Y'), ACUTE_UC), 'Y')
    add(0xE3, '\u00D6', 'Odiaeresis', cp437(0x99), 'O')  # CP437 has this
    add(0xE4, '\u00DC', 'Udiaeresis', cp437(0x9A), 'U')  # CP437 has this
    add(0xE5, '\u00C4', 'Adiaeresis', cp437(0x8E), 'A')  # CP437 has this
    add(0xE6, '\u00D4', 'Ocircumflex', apply_accent_above(ord('O'), [(0,0),(1,0),(2,0),(3,0),(4,0)]), 'O')
    add(0xE7, '\u00C2', 'Acircumflex', apply_accent_above(ord('A'), [(0,0),(1,0),(2,0),(3,0),(4,0)]), 'A')
    add(0xE8, '\u00CE', 'Icircumflex', apply_accent_above(ord('I'), [(0,0),(1,0),(2,0),(3,0),(4,0)]), 'I')

    # --- Additional common characters ---
    add(0xE9, '\u00DF', 'eszett',  cp437(0xE1), 's')  # ß from CP437 pos 0xE1

    return glyphs


# ---------------------------------------------------------------------------
# Preview
# ---------------------------------------------------------------------------

def preview_glyph(char, glyph, label=""):
    """Print ASCII art preview of a 5x8 glyph."""
    hdr = f"'{char}' U+{ord(char):04X}"
    if label:
        hdr += f"  ({label})"
    print(f"  {hdr}:")
    for row in range(8):
        line = "    "
        for col in range(5):
            line += "\u2588" if has_pixel(glyph, col, row) else "\u00B7"
        print(line)
    print()


def generate_preview_image(font_data, ce_glyphs, output_path, zoom=8):
    """Generate a PNG preview comparing base ASCII letter vs CE variant."""
    from PIL import Image, ImageFont, ImageDraw

    items = sorted(ce_glyphs.items())  # sorted by slot
    n = len(items)

    cell_w = zoom * 5 + 4
    cell_h = zoom * 8 + 4
    label_w = 220
    row_w = 2 * cell_w + label_w + 20
    img_h = n * cell_h + 50

    img = Image.new('RGB', (row_w, img_h), (255, 255, 255))
    draw = ImageDraw.Draw(img)

    try:
        lbl_font = ImageFont.truetype(
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf", 13)
    except OSError:
        lbl_font = ImageFont.load_default()

    draw.text((5, 5), "5x7 Font Preview: Base ASCII (grey) vs CE Character (black)",
              fill=(0, 0, 0), font=lbl_font)
    draw.text((5, 22),
              f"Zoom: {zoom}x  |  {n} characters  |  Slots 0x81-0x{items[-1][0]:02X}",
              fill=(100, 100, 100), font=lbl_font)
    y0 = 48

    for idx, (slot, (glyph, char, desc, base)) in enumerate(items):
        y = y0 + idx * cell_h
        # grid line
        draw.line([(0, y), (row_w, y)], fill=(230, 230, 230))

        # Base glyph (grey)
        base_code = ord(base) if len(base) == 1 and base.isascii() else ord('?')
        base_bytes = font_data[base_code * 5: base_code * 5 + 5]
        _draw_glyph(draw, 4, y + 2, base_bytes, zoom, (160, 160, 160))

        # CE glyph (black)
        _draw_glyph(draw, 4 + cell_w, y + 2, glyph, zoom, (0, 0, 0))

        # Label
        draw.text((4 + 2 * cell_w + 8, y + cell_h // 2 - 7),
                  f"0x{slot:02X}  {char}  {desc}  (base: {base})",
                  fill=(0, 0, 0), font=lbl_font)

    img.save(output_path)
    print(f"Preview image saved to {output_path} ({row_w}x{img_h})")


def _draw_glyph(draw, x0, y0, col_bytes, zoom, color):
    for col in range(5):
        b = col_bytes[col]
        for row in range(8):
            if b & (1 << row):
                px, py = x0 + col * zoom, y0 + row * zoom
                draw.rectangle([px, py, px + zoom - 1, py + zoom - 1], fill=color)


# ---------------------------------------------------------------------------
# Output generation
# ---------------------------------------------------------------------------

def build_font_data(ce_glyphs):
    """Build the full 1280-byte font array."""
    data = list(CP437)
    assert len(data) == 1280

    # Slot 0x80: full block (fallback for unmappable chars)
    data[0x80 * 5: 0x80 * 5 + 5] = [0x7F, 0x7F, 0x7F, 0x7F, 0x7F]

    for slot, (glyph, char, desc, base) in ce_glyphs.items():
        data[slot * 5: slot * 5 + 5] = glyph

    return data


def build_codepoint_table(ce_glyphs):
    table = {}
    for slot, (glyph, char, desc, base) in ce_glyphs.items():
        table[ord(char)] = slot
    return table


def format_header(font_data, ce_glyphs, codepoint_table):
    """Generate locale_font.h content."""
    lines = []
    lines.append("// locale_font.h - Custom 5x7 font with Central/Eastern European characters")
    lines.append("// Generated by tools/generate_5x7_font.py - do not edit manually")
    lines.append("//")
    lines.append("// When LOCALE_CE is defined, this replaces the CP437 font in Adafruit GFX")
    lines.append("// via the FONT5X7_H header guard. Included via -include build flag.")
    lines.append("")
    lines.append("#if defined(LOCALE_CE)")
    lines.append("")
    lines.append("#ifndef FONT5X7_H")
    lines.append("#define FONT5X7_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#ifdef __AVR__")
    lines.append("#include <avr/io.h>")
    lines.append("#include <avr/pgmspace.h>")
    lines.append("#elif defined(ESP8266)")
    lines.append("#include <pgmspace.h>")
    lines.append("#elif defined(__IMXRT1052__) || defined(__IMXRT1062__)")
    lines.append("#undef PROGMEM")
    lines.append("#define PROGMEM")
    lines.append("#else")
    lines.append("#define PROGMEM")
    lines.append("#endif")
    lines.append("")
    lines.append("// 0x00-0x7F: Standard ASCII (from CP437)")
    lines.append("// 0x80: Full block (fallback for unmappable non-ASCII)")
    lines.append("// 0x81-0xD0: CE national characters:")
    for slot in sorted(ce_glyphs.keys()):
        glyph, char, desc, base = ce_glyphs[slot]
        lines.append(f"//   0x{slot:02X}: {char} ({desc})")
    lines.append("// 0xEA-0xFF: Original CP437 data (unused)")
    lines.append("")
    lines.append("static const unsigned char font[] PROGMEM = {")

    # CE lookup for labeling
    ce_lookup = {}
    for slot, (glyph, char, desc, base) in ce_glyphs.items():
        ce_lookup[slot] = f"{char} ({desc})"

    for i in range(256):
        b = font_data[i * 5: i * 5 + 5]
        h = ", ".join(f"0x{v:02X}" for v in b)
        comma = "," if i < 255 else ""
        if i == 0x80:
            lines.append(f"    {h}{comma}  // 0x{i:02X}: full block")
        elif i in ce_lookup:
            lines.append(f"    {h}{comma}  // 0x{i:02X}: {ce_lookup[i]}")
        elif 0x20 <= i <= 0x7E:
            lines.append(f"    {h}{comma}  // 0x{i:02X}: '{chr(i)}'")
        else:
            lines.append(f"    {h}{comma}  // 0x{i:02X}")

    lines.append("};")
    lines.append("")
    lines.append("static inline void avoid_unused_const_variable_compiler_warning(void) {")
    lines.append("  (void)font;")
    lines.append("}")
    lines.append("")

    # mapCodepointToSlot function (inside FONT5X7_H guard)
    lines.append("// Map a Unicode codepoint to a font slot (0x81+), or 0x80 (block) if unknown")
    lines.append("static inline uint8_t mapCodepointToSlot(uint16_t cp) {")
    lines.append("  switch (cp) {")
    for cp in sorted(codepoint_table.keys()):
        slot = codepoint_table[cp]
        lines.append(f"    case 0x{cp:04X}: return 0x{slot:02X};  // {chr(cp)}")
    lines.append("    default: return 0x80;  // unmapped -> block")
    lines.append("  }")
    lines.append("}")
    lines.append("")
    lines.append("#endif // FONT5X7_H")
    lines.append("")
    lines.append("#endif // LOCALE_CE")
    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate custom 5x7 font header with CE national characters")
    parser.add_argument("--output", default=None,
                        help="Output .h file (default: print stats only)")
    parser.add_argument("--preview", action="store_true",
                        help="Show ASCII art preview of all generated glyphs")
    parser.add_argument("--preview-image", default=None,
                        help="Generate PNG preview (e.g. --preview-image preview.png)")
    parser.add_argument("--zoom", type=int, default=8,
                        help="Pixel zoom for preview image (default: 8)")
    args = parser.parse_args()

    ce_glyphs = build_ce_glyphs()
    font_data = build_font_data(ce_glyphs)
    codepoint_table = build_codepoint_table(ce_glyphs)

    print(f"CE characters: {len(ce_glyphs)} in slots "
          f"0x{min(ce_glyphs):02X}-0x{max(ce_glyphs):02X}")

    if args.preview:
        print()
        for slot in sorted(ce_glyphs.keys()):
            glyph, char, desc, base = ce_glyphs[slot]
            # Show base letter first for comparison
            base_code = ord(base)
            print(f"  Base '{base}':")
            base_g = get_glyph(base_code)
            for row in range(8):
                line = "    "
                for col in range(5):
                    line += "\u2588" if has_pixel(base_g, col, row) else "\u00B7"
                print(line)
            print()
            preview_glyph(char, glyph, desc)

    if args.preview_image:
        generate_preview_image(font_data, ce_glyphs, args.preview_image,
                               zoom=args.zoom)

    output = format_header(font_data, ce_glyphs, codepoint_table)

    if args.output:
        with open(args.output, 'w') as f:
            f.write(output)
        print(f"Written to {args.output} ({len(output)} bytes)")
    else:
        print(f"Output ready: {len(output)} bytes")
        print("Use --output <path> to write to file")


if __name__ == "__main__":
    main()
