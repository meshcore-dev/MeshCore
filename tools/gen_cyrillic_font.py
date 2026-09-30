#!/usr/bin/env python3
"""Generate src/helpers/ui/CyrillicFontData.cpp.

The stock ThingPulse fonts in src/helpers/ui/OLEDDisplayFonts.cpp map codes
0x80..0xFF to ISO-8859-1, which has no Cyrillic. This script re-lays the upper
half of those tables out as Windows-1251, which carries the full Cyrillic
alphabet, and emits the result as a drop-in extra font.

Codes 0x20..0x7F are copied out of the stock tables byte-for-byte, so Latin text
renders exactly as it does today. Codes 0x80..0xFF are rasterised from Arial
with FreeType's monochrome hinted renderer, which reproduces the stock glyphs to
within 1% of pixels -- the stock tables were evidently produced the same way.

Usage:
    pip install pillow
    python3 tools/gen_cyrillic_font.py [--ttf /path/to/Arial.ttf]
"""

import argparse
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STOCK = os.path.join(REPO, "src/helpers/ui/OLEDDisplayFonts.cpp")
OUT = os.path.join(REPO, "src/helpers/ui/CyrillicFontData.cpp")
DEFAULT_TTF = "/System/Library/Fonts/Supplemental/Arial.ttf"

# stock font table -> the Arial pixel size it was rasterised at
FONTS = [("ArialMT_Plain_16", 16, "ArialMT_Plain_16_Cyr"),
         ("ArialMT_Plain_24", 24, "ArialMT_Plain_24_Cyr")]

PAD = 16  # left margin when rasterising, so negative bearings do not clip


# --------------------------------------------------------------------------
# stock font table parsing
# --------------------------------------------------------------------------

class Font:
    """A ThingPulse OLEDDisplay font table.

    Layout: width, height, first char, char count, then 4 bytes per char
    (offset MSB, offset LSB, data size, advance width), then the glyph data.
    Each glyph is column-major, one byte per 8 rows, LSB = topmost pixel;
    trailing all-zero bytes are trimmed off.
    """

    def __init__(self, values):
        self.width, self.height, self.first, self.num = values[0:4]
        self.jump = values[4:4 + self.num * 4]
        self.data = values[4 + self.num * 4:]
        self.raster = 1 + ((self.height - 1) >> 3)

    @classmethod
    def parse(cls, path, name):
        text = open(path).read()
        m = re.search(r"const uint8_t %s\[\] PROGMEM = \{(.*?)\n\};" % name, text, re.S)
        if not m:
            raise SystemExit("could not find %s in %s" % (name, path))
        body = re.sub(r"//[^\n]*", "", m.group(1))
        return cls([int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body)])

    def glyph(self, code):
        i = (code - self.first) * 4
        msb, lsb, size, width = self.jump[i:i + 4]
        if msb == 0xFF and lsb == 0xFF:
            return None, width
        off = (msb << 8) | lsb
        return self.data[off:off + size], width


# --------------------------------------------------------------------------
# rasterising
# --------------------------------------------------------------------------

def cp1251_char(code):
    try:
        return bytes([code]).decode("cp1251")
    except UnicodeDecodeError:
        return None


def rasterise(ttf, char, height):
    """Render one character; returns (rows, advance) or None if it has no glyph."""
    advance = round(ttf.getlength(char))
    if advance <= 0:
        return None
    img = Image.new("1", (2 * PAD + advance, height + 2 * PAD), 0)
    draw = ImageDraw.Draw(img)
    draw.fontmode = "1"  # monochrome, hinted -- matches the stock tables
    draw.text((PAD, 0), char, font=ttf, fill=1)
    px = img.load()
    return [[bool(px[PAD + x, y]) for x in range(advance)] for y in range(height)], advance


def encode(rows, width, height):
    raster = 1 + ((height - 1) >> 3)
    out = []
    for x in range(width):
        for band in range(raster):
            byte = 0
            for bit in range(8):
                y = band * 8 + bit
                if y < height and rows[y][x]:
                    byte |= 1 << bit
            out.append(byte)
    while out and out[-1] == 0:
        out.pop()
    return out


def build(stock_name, size, ttf_path):
    stock = Font.parse(STOCK, stock_name)
    ttf = ImageFont.truetype(ttf_path, size)

    glyphs = []
    for code in range(stock.first, stock.first + stock.num):
        if code < 0x80:
            data, width = stock.glyph(code)  # verbatim, so Latin text does not shift
            glyphs.append((code, data, width))
            continue
        char = cp1251_char(code)
        raster = rasterise(ttf, char, stock.height) if char else None
        if raster is None:
            glyphs.append((code, None, 0))
            continue
        rows, width = raster
        data = encode(rows, width, stock.height)
        glyphs.append((code, data or None, width))

    jump, data = [], []
    for code, glyph, width in glyphs:
        if glyph is None:
            jump.append((0xFF, 0xFF, 0, width))
            continue
        offset = len(data)
        if offset > 0xFFFF:
            raise SystemExit("%s: glyph data exceeds the 16-bit jump table" % stock_name)
        jump.append((offset >> 8, offset & 0xFF, len(glyph), width))
        data.extend(glyph)
    return stock, jump, data, max(w for _, _, w in glyphs)


# --------------------------------------------------------------------------
# emitting
# --------------------------------------------------------------------------

def emit_font(out, name, stock, jump, data, max_width):
    out.append("const uint8_t %s[] PROGMEM = {" % name)
    out.append("  0x%02X, // Width: %d" % (max_width, max_width))
    out.append("  0x%02X, // Height: %d" % (stock.height, stock.height))
    out.append("  0x%02X, // First Char: %d" % (stock.first, stock.first))
    out.append("  0x%02X, // Numbers of Chars: %d" % (stock.num, stock.num))
    out.append("")
    out.append("  // Jump Table:")
    for i, (msb, lsb, size, width) in enumerate(jump):
        code = stock.first + i
        char = cp1251_char(code)
        label = ""
        if char and code >= 0x80 and char.isprintable() and not char.isspace():
            label = " %s" % char
        out.append("  0x%02X, 0x%02X, 0x%02X, 0x%02X,  // %d:%d%s"
                   % (msb, lsb, size, width, code, (msb << 8) | lsb, label))
    out.append("")
    out.append("  // Font Data:")
    for i in range(0, len(data), 12):
        out.append("  " + "".join("0x%02X," % b for b in data[i:i + 12]))
    out[-1] = out[-1].rstrip(",")
    out.append("};")
    out.append("")


def emit_unicode_map(out):
    """Reverse map: Unicode code point -> Windows-1251 byte, sorted for bsearch."""
    pairs = []
    for code in range(0x80, 0x100):
        char = cp1251_char(code)
        if char is not None:
            pairs.append((ord(char), code))
    pairs.sort()

    out.append("// Unicode -> Windows-1251, sorted by code point (see cyrillicFontLookup).")
    out.append("const uint16_t CYRILLIC_UNICODE[] PROGMEM = {")
    for i in range(0, len(pairs), 8):
        out.append("  " + "".join("0x%04X," % u for u, _ in pairs[i:i + 8]))
    out[-1] = out[-1].rstrip(",")
    out.append("};")
    out.append("")
    out.append("const uint8_t CYRILLIC_CP1251[] PROGMEM = {")
    for i in range(0, len(pairs), 12):
        out.append("  " + "".join("0x%02X," % c for _, c in pairs[i:i + 12]))
    out[-1] = out[-1].rstrip(",")
    out.append("};")
    out.append("")
    out.append("const uint16_t CYRILLIC_MAP_LEN = %d;" % len(pairs))
    out.append("")


HEADER = """\
// Windows-1251 (Cyrillic) variants of the stock ThingPulse Arial fonts.
//
// GENERATED FILE -- regenerate with tools/gen_cyrillic_font.py, do not hand edit.
//
// Codes 0x20..0x7F hold the stock ArialMT_Plain_* glyphs byte-for-byte, so Latin
// text is pixel-identical to the stock fonts. Codes 0x80..0xFF follow Windows-1251,
// which covers the whole Cyrillic alphabet. Feed UTF-8 text in through
// cyrillicFontLookup() -- see CyrillicFont.cpp.

#include "CyrillicFont.h"
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", default=DEFAULT_TTF, help="Arial TTF to rasterise from")
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()

    if not os.path.exists(args.ttf):
        raise SystemExit("no such font file: %s (pass --ttf)" % args.ttf)

    out = [HEADER, ""]
    emit_unicode_map(out)
    for stock_name, size, cyr_name in FONTS:
        stock, jump, data, max_width = build(stock_name, size, args.ttf)
        sys.stderr.write("%s: %d glyph bytes (%s: %d), max width %d\n"
                         % (cyr_name, len(data), stock_name, len(stock.data), max_width))
        emit_font(out, cyr_name, stock, jump, data, max_width)

    open(args.out, "w").write("\n".join(out))
    sys.stderr.write("wrote %s\n" % args.out)


main()
