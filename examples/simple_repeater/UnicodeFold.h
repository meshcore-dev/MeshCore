#pragma once

#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>

// Best-effort Unicode confusable folding for the channel content filter.
//
// foldUtf8() normalises a UTF-8 string so that homoglyph / look-alike tricks
// don't slip past a keyword or sender blocklist. It:
//   - folds confusable codepoints (fullwidth, mathematical alphanumerics,
//     circled / squared / parenthesized letters, regional-indicator "flag"
//     letters, common Cyrillic & Greek look-alikes, accented Latin) to the
//     plain ASCII letter they imitate,
//   - drops invisible codepoints (zero-width spaces/joiners, combining marks,
//     variation selectors, bidi controls, emoji skin-tone modifiers, ...),
//   - lowercases ASCII,
//   - passes any other codepoint through unchanged (so non-Latin blocklist
//     terms still match, and visible symbols still act as word separators).
//
// This is not a full UTS#39 skeleton — it covers the abuse vectors that show
// up in practice without shipping the entire Unicode confusables table.

namespace ufold {

// Decode one codepoint. *len is bytes consumed (>=1). Malformed -> raw byte.
static inline uint32_t utf8Next(const uint8_t* s, int* len) {
  uint8_t c = s[0];
  if (c < 0x80) { *len = 1; return c; }
  if ((c & 0xE0) == 0xC0) {
    if ((s[1] & 0xC0) == 0x80) { *len = 2; return ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F); }
  } else if ((c & 0xF0) == 0xE0) {
    if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
      *len = 3;
      return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
  } else if ((c & 0xF8) == 0xF0) {
    if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
      *len = 4;
      return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
             ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
  }
  *len = 1;
  return c;  // malformed sequence
}

// Invisible / formatting / combining codepoints that should be stripped.
static inline bool isDrop(uint32_t cp) {
  if (cp == 0x00AD) return true;                   // soft hyphen
  if (cp >= 0x0300 && cp <= 0x036F) return true;   // combining diacritical marks
  if (cp >= 0x1AB0 && cp <= 0x1AFF) return true;   // combining marks extended
  if (cp >= 0x1DC0 && cp <= 0x1DFF) return true;   // combining marks supplement
  if (cp >= 0x200B && cp <= 0x200F) return true;   // ZW space/joiners, LRM/RLM
  if (cp >= 0x202A && cp <= 0x202E) return true;   // bidi embeddings/overrides
  if (cp >= 0x2060 && cp <= 0x2064) return true;   // word joiner, invisible ops
  if (cp >= 0x20D0 && cp <= 0x20FF) return true;   // combining marks for symbols
  if (cp >= 0xFE00 && cp <= 0xFE0F) return true;   // variation selectors
  if (cp >= 0xFE20 && cp <= 0xFE2F) return true;   // combining half marks
  if (cp == 0xFEFF) return true;                   // BOM / ZW no-break space
  if (cp >= 0x1F3FB && cp <= 0x1F3FF) return true; // emoji skin-tone modifiers
  if (cp >= 0xE0000 && cp <= 0xE007F) return true; // tags
  return false;
}

static inline char foldLatinExtA(uint32_t cp) {
  // U+0100..U+017F, indexed base letter (already lowercase)
  static const char* T =
      "aaaaaa"        // 0100-0105
      "cccccccc"      // 0106-010D
      "dddd"          // 010E-0111
      "eeeeeeeeee"    // 0112-011B
      "gggggggg"      // 011C-0123
      "hhhh"          // 0124-0127
      "iiiiiiiiii"    // 0128-0131
      "ii"            // 0132-0133
      "jj"            // 0134-0135
      "kkk"           // 0136-0138
      "llllllllll"    // 0139-0142
      "nnnnnnnnn"     // 0143-014B
      "oooooo"        // 014C-0151
      "oo"            // 0152-0153
      "rrrrrr"        // 0154-0159
      "ssssssss"      // 015A-0161
      "tttttt"        // 0162-0167
      "uuuuuuuuuuuu"  // 0168-0173
      "ww"            // 0174-0175
      "yyy"           // 0176-0178
      "zzzzzz"        // 0179-017E
      "s";            // 017F
  return T[cp - 0x0100];
}

static inline char foldLatin1(uint32_t cp) {
  if (cp >= 0xC0 && cp <= 0xC6) return 'a';
  if (cp == 0xC7) return 'c';
  if (cp >= 0xC8 && cp <= 0xCB) return 'e';
  if (cp >= 0xCC && cp <= 0xCF) return 'i';
  if (cp == 0xD0) return 'd';
  if (cp == 0xD1) return 'n';
  if (cp >= 0xD2 && cp <= 0xD6) return 'o';
  if (cp == 0xD8) return 'o';
  if (cp >= 0xD9 && cp <= 0xDC) return 'u';
  if (cp == 0xDD) return 'y';
  if (cp == 0xDE) return 't';
  if (cp == 0xDF) return 's';
  if (cp >= 0xE0 && cp <= 0xE6) return 'a';
  if (cp == 0xE7) return 'c';
  if (cp >= 0xE8 && cp <= 0xEB) return 'e';
  if (cp >= 0xEC && cp <= 0xEF) return 'i';
  if (cp == 0xF0) return 'd';
  if (cp == 0xF1) return 'n';
  if (cp >= 0xF2 && cp <= 0xF6) return 'o';
  if (cp == 0xF8) return 'o';
  if (cp >= 0xF9 && cp <= 0xFC) return 'u';
  if (cp == 0xFD || cp == 0xFF) return 'y';
  if (cp == 0xFE) return 't';
  return 0;  // ×, ÷, etc.
}

static inline char foldMathAlnum(uint32_t cp) {
  // Mathematical digits: 5 styles of 0-9
  if (cp >= 0x1D7CE && cp <= 0x1D7FF) return '0' + ((cp - 0x1D7CE) % 10);
  // Latin letter styles: each block is 52 wide (A-Z then a-z)
  static const uint32_t starts[] = {
      0x1D400, 0x1D434, 0x1D468, 0x1D49C, 0x1D4D0, 0x1D504, 0x1D538,
      0x1D56C, 0x1D5A0, 0x1D5D4, 0x1D608, 0x1D63C, 0x1D670};
  for (unsigned k = 0; k < sizeof(starts) / sizeof(starts[0]); k++) {
    if (cp >= starts[k] && cp <= starts[k] + 51) {
      return 'a' + ((cp - starts[k]) % 26);
    }
  }
  if (cp == 0x1D6A4) return 'i';  // italic dotless i
  if (cp == 0x1D6A5) return 'j';  // italic dotless j
  return 0;
}

static inline char foldEnclosed(uint32_t cp) {
  if (cp >= 0x249C && cp <= 0x24B5) return 'a' + (cp - 0x249C); // parenthesized small
  if (cp >= 0x24B6 && cp <= 0x24CF) return 'a' + (cp - 0x24B6); // circled capital
  if (cp >= 0x24D0 && cp <= 0x24E9) return 'a' + (cp - 0x24D0); // circled small
  if (cp >= 0x2460 && cp <= 0x2468) return '1' + (cp - 0x2460); // circled 1-9
  if (cp == 0x24EA) return '0';                                 // circled 0
  return 0;
}

static inline char foldEnclosedSupp(uint32_t cp) {
  if (cp >= 0x1F110 && cp <= 0x1F129) return 'a' + (cp - 0x1F110); // parenthesized
  if (cp >= 0x1F130 && cp <= 0x1F149) return 'a' + (cp - 0x1F130); // squared
  if (cp >= 0x1F150 && cp <= 0x1F169) return 'a' + (cp - 0x1F150); // negative circled
  if (cp >= 0x1F170 && cp <= 0x1F189) return 'a' + (cp - 0x1F170); // negative squared
  if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return 'a' + (cp - 0x1F1E6); // regional indicators
  return 0;
}

static inline char foldLetterlike(uint32_t cp) {
  switch (cp) {
    case 0x2102: case 0x212D: return 'c';
    case 0x210A: return 'g';
    case 0x210B: case 0x210C: case 0x210D: case 0x210E: case 0x210F: return 'h';
    case 0x2110: case 0x2111: return 'i';
    case 0x2112: case 0x2113: return 'l';
    case 0x2115: return 'n';
    case 0x2118: case 0x2119: return 'p';
    case 0x211A: return 'q';
    case 0x211B: case 0x211C: case 0x211D: return 'r';
    case 0x2124: return 'z';
    case 0x212C: return 'b';
    case 0x212F: case 0x2130: return 'e';
    case 0x2131: return 'f';
    case 0x2133: return 'm';
    case 0x2134: return 'o';
    default: return 0;
  }
}

static inline char foldCyrillic(uint32_t cp) {
  switch (cp) {
    case 0x0410: case 0x0430: return 'a'; // А а
    case 0x0412: case 0x0432: return 'b'; // В в
    case 0x0415: case 0x0435: return 'e'; // Е е
    case 0x0405: case 0x0455: return 's'; // Ѕ ѕ
    case 0x0406: case 0x0456: return 'i'; // І і
    case 0x0408: case 0x0458: return 'j'; // Ј ј
    case 0x041A: case 0x043A: return 'k'; // К к
    case 0x041C: case 0x043C: return 'm'; // М м
    case 0x041D: case 0x043D: return 'h'; // Н н
    case 0x041E: case 0x043E: return 'o'; // О о
    case 0x0420: case 0x0440: return 'p'; // Р р
    case 0x0421: case 0x0441: return 'c'; // С с
    case 0x0422: case 0x0442: return 't'; // Т т
    case 0x0423: case 0x0443: return 'y'; // У у
    case 0x0425: case 0x0445: return 'x'; // Х х
    default: return 0;
  }
}

static inline char foldGreek(uint32_t cp) {
  switch (cp) {
    case 0x0391: return 'a'; case 0x0392: return 'b'; case 0x0395: return 'e';
    case 0x0396: return 'z'; case 0x0397: return 'h'; case 0x0399: return 'i';
    case 0x039A: return 'k'; case 0x039C: return 'm'; case 0x039D: return 'n';
    case 0x039F: return 'o'; case 0x03A1: return 'p'; case 0x03A4: return 't';
    case 0x03A5: return 'y'; case 0x03A7: return 'x';
    case 0x03BF: return 'o'; case 0x03B9: return 'i'; case 0x03C1: return 'p';
    default: return 0;
  }
}

// Fold one codepoint to a lowercase ASCII letter/digit, or 0 if not foldable.
static inline char foldLetter(uint32_t cp) {
  char c;
  if (cp >= 0xFF21 && cp <= 0xFF3A) return 'a' + (cp - 0xFF21); // fullwidth A-Z
  if (cp >= 0xFF41 && cp <= 0xFF5A) return 'a' + (cp - 0xFF41); // fullwidth a-z
  if (cp >= 0xFF10 && cp <= 0xFF19) return '0' + (cp - 0xFF10); // fullwidth 0-9
  if (cp >= 0x00C0 && cp <= 0x00FF) return foldLatin1(cp);
  if (cp >= 0x0100 && cp <= 0x017F) return foldLatinExtA(cp);
  if (cp >= 0x0391 && cp <= 0x03C9 && (c = foldGreek(cp))) return c;
  if (cp >= 0x0400 && cp <= 0x04FF && (c = foldCyrillic(cp))) return c;
  if (cp >= 0x2100 && cp <= 0x214F && (c = foldLetterlike(cp))) return c;
  if (cp >= 0x2460 && cp <= 0x24FF && (c = foldEnclosed(cp))) return c;
  if (cp >= 0x1D400 && cp <= 0x1D7FF && (c = foldMathAlnum(cp))) return c;
  if (cp >= 0x1F100 && cp <= 0x1F1FF && (c = foldEnclosedSupp(cp))) return c;
  return 0;
}

// Fold a null-terminated UTF-8 string into a normalized lowercase buffer.
static inline void foldUtf8(const char* in, char* out, size_t out_size) {
  const uint8_t* s = (const uint8_t*)in;
  size_t o = 0;
  if (out_size == 0) return;
  while (*s && o + 1 < out_size) {
    int len = 1;
    uint32_t cp = utf8Next(s, &len);
    char f;
    if (cp < 0x80) {
      out[o++] = (char)tolower((int)cp);
    } else if (isDrop(cp)) {
      // skip
    } else if ((f = foldLetter(cp)) != 0) {
      out[o++] = f;
    } else {
      for (int k = 0; k < len && o + 1 < out_size; k++) out[o++] = (char)s[k]; // pass through
    }
    s += len;
  }
  out[o] = 0;
}

}  // namespace ufold
