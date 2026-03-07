#!/usr/bin/env python3
"""
Generate complete OLEDDisplay font arrays from a TTF font file.

Usage:
    python3 tools/generate_font_glyphs.py --preview --locale LOCALE_PL
    python3 tools/generate_font_glyphs.py --output src/helpers/ui/OLEDDisplayFonts.cpp

Generates all 3 font sizes (Plain_10, Plain_16, Plain_24) from scratch using
a TTF font. For each LOCALE_XX, national characters are placed in slots
0x81+. The output is a complete OLEDDisplayFonts.cpp with #if/#elif/#else
blocks: locale variants use the generated font, #else keeps the original
ArialMT data as fallback.

OLEDDisplay bitmap format (column-major):
  - Header: 4 bytes (maxWidth, height, firstChar, numChars)
  - Jump table: numChars * 4 bytes (offsetMSB, offsetLSB, byteSize, charWidth)
    - 0xFF,0xFF = empty/undrawable glyph
  - Bitmap data: column by column, ceil(height/8) bytes per column
    - bit 0 = top pixel, bit 7 = bottom pixel of each 8px segment
"""

import sys
import re
import argparse
from PIL import Image, ImageFont, ImageDraw

# Character range: 0x20..0xFF (224 chars), matching original ArialMT layout
FIRST_CHAR = 0x20
NUM_CHARS = 0xE0  # 224

# Slots 0x81..0xA0 are free for locale chars (0x80 = Euro)
FIRST_LOCALE_SLOT = 0x81

# Font size configs: (array_name, target_height)
# target_height values taken from the original ArialMT font headers
FONT_CONFIGS = [
    ("ArialMT_Plain_10", 13),
    ("ArialMT_Plain_16", 19),
    ("ArialMT_Plain_24", 28),
]

LOCALES = {
    "LOCALE_PL": list("ĄąĆćĘęŁłŃńŚśŹźŻż"),
    "LOCALE_CZ": list("ČčĎďĚěĹĺĽľŇňŔŕŘřŠšŤťŮůŽž"),
    "LOCALE_HU": list("ŐőŰű"),
    "LOCALE_RO": list("ĂăȘșȚț"),
    "LOCALE_HR": list("ČčĐđŠšŽž"),
    "LOCALE_TR": list("ĞğİıŞş"),
    "LOCALE_LT": list("ĄąČčĖėĘęĮįŠšŪūŲųŽž"),
    "LOCALE_LV": list("ĀāČčĒēĢģĪīĶķĻļŅņŠšŪūŽž"),
    "LOCALE_EE": list("ŠšŽž"),
}

DEFAULT_FONT = "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"
DEFAULT_INPUT = "src/helpers/ui/OLEDDisplayFonts.cpp"


def find_font_size(font_path, target_height):
    """Find the pixel size that fits in target_height (ascent+descent <= target)."""
    for px_size in range(target_height + 10, 4, -1):
        font = ImageFont.truetype(font_path, px_size)
        ascent, descent = font.getmetrics()
        if ascent + descent <= target_height:
            return px_size
    return target_height


def render_glyph(font, char, target_height):
    """Render one character. Returns (advance_width, bitmap_width, columns).

    The bitmap may be wider than the advance width if ink extends beyond.
    columns: list of lists of bools, [col][row], row 0 = top.
    """
    # Large canvas to avoid any clipping
    margin = target_height * 2
    canvas_w = margin * 2 + target_height * 2
    canvas_h = margin * 2 + target_height
    img = Image.new('1', (canvas_w, canvas_h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((margin, margin), char, font=font, fill=1)

    pixels = img.load()
    advance = int(round(font.getlength(char)))
    if advance < 1:
        advance = 1

    # Find actual rightmost pixel relative to draw origin
    rightmost = 0
    for x in range(margin, margin + advance + target_height):
        for y in range(margin, margin + target_height):
            if x < canvas_w and pixels[x, y]:
                col_idx = x - margin
                if col_idx > rightmost:
                    rightmost = col_idx

    # Bitmap width: include all visible pixels
    bitmap_width = max(advance, rightmost + 1)

    columns = []
    for x in range(margin, margin + bitmap_width):
        col = []
        for y in range(margin, margin + target_height):
            if x < canvas_w:
                col.append(bool(pixels[x, y]))
            else:
                col.append(False)
        columns.append(col)

    return advance, bitmap_width, columns


def columns_to_bytes(columns, target_height):
    """Convert column pixel data to OLEDDisplay packed bytes."""
    raster_height = (target_height + 7) // 8
    result = []
    for col in columns:
        for seg in range(raster_height):
            byte_val = 0
            for bit in range(8):
                py = seg * 8 + bit
                if py < len(col) and col[py]:
                    byte_val |= (1 << bit)
            result.append(byte_val)
    return result


def preview_glyph(char, columns, target_height, advance=None):
    """Print ASCII art preview."""
    w = len(columns)
    label = f"'{char}' U+{ord(char):04X}" if ord(char) >= 32 else f"U+{ord(char):04X}"
    extra = f" adv={advance}" if advance and advance != w else ""
    print(f"  {label} ({w}x{target_height}{extra}):")
    for y in range(target_height):
        row = "    "
        for x in range(w):
            row += "\u2588" if columns[x][y] else "\u00B7"
        print(row)
    print()


def generate_full_font(font_path, target_height, locale_chars=None, preview=False):
    """Generate a complete font array (header + jump table + bitmap data).

    Args:
        font_path: path to TTF font
        target_height: pixel height for the font
        locale_chars: dict mapping slot_index -> unicode char, or None
        preview: show ASCII previews of locale chars

    Returns: list of bytes (complete font array)
    """
    px_size = find_font_size(font_path, target_height)
    font = ImageFont.truetype(font_path, px_size)

    # Generate glyphs for all 224 characters
    glyph_data = []  # list of (width, bitmap_bytes) or None for empty
    max_width = 0

    for i in range(NUM_CHARS):
        char_code = FIRST_CHAR + i

        # Check if this slot has a locale character override
        if locale_chars and char_code in locale_chars:
            char = locale_chars[char_code]
        elif char_code <= 0x7E:
            # Standard ASCII printable
            char = chr(char_code)
        elif char_code == 0x7F:
            # DEL - empty
            glyph_data.append(None)
            continue
        elif char_code == 0x80:
            # Euro sign
            char = "\u20AC"
        elif char_code >= 0x81 and char_code <= 0xA0:
            # Empty slot (unless locale provides it)
            if not (locale_chars and char_code in locale_chars):
                glyph_data.append(None)
                continue
        elif char_code >= 0xA1:
            # Latin-1 Supplement
            char = chr(char_code)
        else:
            glyph_data.append(None)
            continue

        advance, bitmap_width, columns = render_glyph(font, char, target_height)

        if preview and locale_chars and char_code in locale_chars:
            preview_glyph(char, columns, target_height, advance)

        bitmap_bytes = columns_to_bytes(columns, target_height)

        if advance > max_width:
            max_width = advance

        glyph_data.append((advance, bitmap_bytes))

    # Build the font array
    # Header
    result = [max_width, target_height, FIRST_CHAR, NUM_CHARS]

    # Collect bitmap data and build jump table
    jump_table = []
    all_bitmaps = bytearray()

    for i, glyph in enumerate(glyph_data):
        char_code = FIRST_CHAR + i

        if glyph is None:
            # Empty slot - determine width for spacing
            if char_code == 0x20:
                # Space character - use a reasonable width
                space_w = int(round(font.getlength(' ')))
                jump_table.extend([0xFF, 0xFF, 0x00, space_w])
            elif char_code <= 0x7F:
                jump_table.extend([0xFF, 0xFF, 0x00, 0x00])
            else:
                # Match original behavior: empty slots have width from original
                # For simplicity use max_width for wider spacing
                jump_table.extend([0xFF, 0xFF, 0x00, max_width])
            continue

        width, bitmap_bytes = glyph
        offset = len(all_bitmaps)
        msb = (offset >> 8) & 0xFF
        lsb = offset & 0xFF
        byte_size = len(bitmap_bytes)

        jump_table.extend([msb, lsb, byte_size, width])
        all_bitmaps.extend(bitmap_bytes)

    result.extend(jump_table)
    result.extend(all_bitmaps)
    return result


def format_c_array(name, data, target_height):
    """Format font byte array as C code."""
    header_w, header_h = data[0], data[1]
    first_char, num_chars = data[2], data[3]

    lines = []
    lines.append(f"const uint8_t {name}[] PROGMEM = {{")
    lines.append(f"  0x{data[0]:02X}, // Width: {data[0]}")
    lines.append(f"  0x{data[1]:02X}, // Height: {data[1]}")
    lines.append(f"  0x{data[2]:02X}, // First Char: {data[2]}")
    lines.append(f"  0x{data[3]:02X}, // Numbers of Chars: {data[3]}")
    lines.append("")
    lines.append("  // Jump Table:")

    for i in range(num_chars):
        off = 4 + i * 4
        msb, lsb, bsz, w = data[off], data[off+1], data[off+2], data[off+3]
        char_code = first_char + i
        addr = (msb << 8) | lsb
        lines.append(f"  0x{msb:02X}, 0x{lsb:02X}, 0x{bsz:02X}, 0x{w:02X},  // {char_code}:{addr}")

    lines.append("")

    # Bitmap data
    bitmap_start = 4 + num_chars * 4
    bitmap = data[bitmap_start:]

    if not bitmap:
        lines.append("};")
        return "\n".join(lines)

    # Format bitmap data in rows
    for i in range(0, len(bitmap), 10):
        chunk = bitmap[i:i+10]
        hex_vals = ",".join(f"0x{b:02X}" for b in chunk)
        if i + 10 >= len(bitmap):
            lines.append(f"  {hex_vals}")
        else:
            lines.append(f"  {hex_vals},")

    lines.append("};")
    return "\n".join(lines)


def read_original_arrays(input_path):
    """Read the original font arrays from OLEDDisplayFonts.cpp as raw text blocks."""
    with open(input_path) as f:
        content = f.read()

    originals = {}
    for name, _ in FONT_CONFIGS:
        pattern = rf'(const\s+uint8_t\s+{name}\[\]\s+PROGMEM\s*=\s*\{{.*?\}};)'
        m = re.search(pattern, content, re.DOTALL)
        if m:
            originals[name] = m.group(1)
        else:
            print(f"WARNING: Could not find {name} in {input_path}")
    return originals


def main():
    parser = argparse.ArgumentParser(
        description="Generate complete OLEDDisplay font arrays from TTF")
    parser.add_argument("--font", default=DEFAULT_FONT,
                        help="Path to TTF font file")
    parser.add_argument("--input", default=DEFAULT_INPUT,
                        help="Existing OLEDDisplayFonts.cpp (for #else fallback)")
    parser.add_argument("--output", default=None,
                        help="Output file path")
    parser.add_argument("--preview", action="store_true",
                        help="Show ASCII art preview of locale glyphs (all 3 sizes)")
    parser.add_argument("--preview-ascii", action="store_true",
                        help="Also preview some standard ASCII chars for comparison")
    parser.add_argument("--locale", default=None,
                        help="Generate only this locale (e.g. LOCALE_PL)")
    args = parser.parse_args()

    print(f"Font:  {args.font}")
    print(f"Input: {args.input}")
    print()

    # Calibration info
    print("Font size calibration:")
    for name, th in FONT_CONFIGS:
        px = find_font_size(args.font, th)
        f = ImageFont.truetype(args.font, px)
        asc, desc = f.getmetrics()
        print(f"  {name}: target_h={th}, px_size={px}, "
              f"ascent={asc}, descent={desc}, total={asc+desc}")
    print()

    # Read original arrays for #else fallback
    originals = read_original_arrays(args.input)

    # Select locales
    locales = LOCALES
    if args.locale:
        if args.locale not in LOCALES:
            print(f"Unknown locale: {args.locale}")
            print(f"Available: {', '.join(LOCALES.keys())}")
            sys.exit(1)
        locales = {args.locale: LOCALES[args.locale]}

    # Preview standard ASCII chars if requested
    if args.preview_ascii:
        print("=== Standard ASCII reference chars ===")
        ref_chars = list("ABCDEabcde0123")
        for font_name, th in FONT_CONFIGS:
            print(f"\n  --- {font_name} (height={th}) ---")
            px = find_font_size(args.font, th)
            f = ImageFont.truetype(args.font, px)
            for ch in ref_chars:
                adv, bw, cols = render_glyph(f, ch, th)
                preview_glyph(ch, cols, th, adv)
        print()

    # Generate fonts for each locale
    # locale_name -> font_name -> byte array
    locale_font_data = {}
    for locale_name, chars in locales.items():
        print(f"Generating {locale_name}: {len(chars)} chars ({' '.join(chars)})")
        locale_font_data[locale_name] = {}

        # Map locale chars to their slot positions
        locale_map = {}
        for i, char in enumerate(chars):
            slot = FIRST_LOCALE_SLOT + i
            locale_map[slot] = char

        for font_name, th in FONT_CONFIGS:
            if args.preview:
                print(f"\n  --- {font_name} (height={th}) ---")
            data = generate_full_font(args.font, th, locale_map, args.preview)
            locale_font_data[locale_name][font_name] = data
            bitmap_start = 4 + NUM_CHARS * 4
            bitmap_size = len(data) - bitmap_start
            print(f"  {font_name}: {len(data)} bytes total, {bitmap_size} bytes bitmap")
        print()

    # Build output
    out_lines = []
    out_lines.append('#include "OLEDDisplayFonts.h"')
    out_lines.append("")

    for font_name, th in FONT_CONFIGS:
        first = True
        for locale_name in locales:
            directive = "#if" if first else "#elif"
            out_lines.append(f"{directive} defined({locale_name})")
            out_lines.append(format_c_array(
                font_name, locale_font_data[locale_name][font_name], th))
            out_lines.append("")
            first = False

        # #else: original ArialMT
        out_lines.append("#else")
        if font_name in originals:
            out_lines.append(originals[font_name])
        else:
            out_lines.append(f"// WARNING: original {font_name} not found in input")
        out_lines.append("#endif")
        out_lines.append("")

    output_text = "\n".join(out_lines)

    if args.output:
        with open(args.output, 'w') as f:
            f.write(output_text)
        print(f"Written to {args.output} ({len(output_text)} bytes, "
              f"{output_text.count(chr(10))} lines)")
    else:
        total_lines = output_text.count(chr(10))
        print(f"Output ready: {len(output_text)} bytes, {total_lines} lines")
        print("Use --output <path> to write to file")


if __name__ == "__main__":
    main()
