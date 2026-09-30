#include "CyrillicFont.h"

#include <string.h>

// Windows-1251 has no room for the Latin-1 accented letters (Cyrillic occupies
// 0xC0..0xFF), so fold U+00C0..U+00FF down to their unaccented ASCII base. That
// keeps European names readable instead of turning them into Cyrillic mojibake.
static const char LATIN1_FOLD[] PROGMEM =
  "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPs"   // U+00C0 .. U+00DF
  "aaaaaaaceeeeiiiidnooooo/ouuuuypy";  // U+00E0 .. U+00FF

static uint8_t unicodeToCp1251(uint32_t cp) {
  if (cp >= 0x0410 && cp <= 0x044F) {   // А..я, the contiguous bulk of the alphabet
    return (uint8_t)(0xC0 + (cp - 0x0410));
  }
  if (cp >= 0x00C0 && cp <= 0x00FF) {
    return (uint8_t) pgm_read_byte(LATIN1_FOLD + (cp - 0x00C0));
  }
  if (cp > 0xFFFF) return 0;

  // binary search the generated table for everything else (Ё, ё, №, °, «, »,
  // the dashes and quotes, and the non-Russian Cyrillic letters)
  uint16_t lo = 0, hi = CYRILLIC_MAP_LEN;
  while (lo < hi) {
    uint16_t mid = (lo + hi) / 2;
    uint16_t val = pgm_read_word(CYRILLIC_UNICODE + mid);
    if (val == cp) return (uint8_t) pgm_read_byte(CYRILLIC_CP1251 + mid);
    if (val < cp) lo = mid + 1; else hi = mid;
  }
  return 0;
}

char cyrillicFontLookup(const uint8_t ch) {
  static uint8_t remaining = 0;   // continuation bytes still expected
  static uint32_t cp = 0;         // code point accumulated so far

  if (ch < 0x80) {                // plain ASCII, also resets a truncated sequence
    remaining = 0;
    return (char) ch;
  }

  if ((ch & 0xC0) == 0x80) {      // continuation byte
    if (remaining == 0) return 0; // stray - drop it
    cp = (cp << 6) | (ch & 0x3F);
    if (--remaining > 0) return 0;
    return (char) unicodeToCp1251(cp);
  }

  if ((ch & 0xE0) == 0xC0)      { cp = ch & 0x1F; remaining = 1; }
  else if ((ch & 0xF0) == 0xE0) { cp = ch & 0x0F; remaining = 2; }
  else if ((ch & 0xF8) == 0xF0) { cp = ch & 0x07; remaining = 3; }
  else                          { remaining = 0; }   // invalid lead byte
  return 0;
}

void cyrillicCopyUTF8(char* dest, const char* src, size_t dest_size) {
  size_t j = 0;
  for (size_t i = 0; src[i] != 0; ) {
    uint8_t c = (uint8_t) src[i];
    if (c < 32 || c == 127) { i++; continue; }        // control character
    if ((c & 0xC0) == 0x80) { i++; continue; }        // stray continuation byte

    size_t len = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    size_t have = 1;
    while (have < len && ((uint8_t) src[i + have] & 0xC0) == 0x80) have++;
    if (have < len) break;                            // sequence cut short in src

    if (j + len >= dest_size) break;                  // leave room for the NUL
    memcpy(&dest[j], &src[i], len);
    j += len;
    i += len;
  }
  dest[j] = 0;
}
