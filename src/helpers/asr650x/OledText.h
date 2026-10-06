#pragma once
#include <stdint.h>
#include <string.h>
#include "Font5x7.h"

namespace asr650x {

const int OLED_COLS = 128;
const int OLED_CHARS = 21;          // 21 characters x 6 columns = 126

// One SSD1306 page (8 pixel rows, 128 columns) holding a line of text; bit 0 of each byte is the top pixel row.
inline void render_line(const char* text, uint8_t page[OLED_COLS]) {
  memset(page, 0, OLED_COLS);
  if (!text) return;
  for (int i = 0; i < OLED_CHARS && text[i]; i++) {
    const uint8_t* g = font5x7_glyph(text[i]);
    memcpy(page + i * 6, g, 5);
  }
}

}  // namespace asr650x
