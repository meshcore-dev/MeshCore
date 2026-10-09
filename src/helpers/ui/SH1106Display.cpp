#include "SH1106Display.h"
#include <Adafruit_GrayOLED.h>
#include "Adafruit_SH110X.h"

bool SH1106Display::i2c_probe(TwoWire &wire, uint8_t addr)
{
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

// Color scheme
ColorVal UIColor::window_bkg = SH110X_BLACK;
ColorVal UIColor::title_bkg = SH110X_BLACK;
ColorVal UIColor::title_txt = SH110X_WHITE;
ColorVal UIColor::primary_txt = SH110X_WHITE;
ColorVal UIColor::secondary_txt = SH110X_WHITE;
ColorVal UIColor::warning_txt = SH110X_WHITE;
ColorVal UIColor::popup_bkg = SH110X_BLACK;
ColorVal UIColor::popup_txt = SH110X_WHITE;
ColorVal UIColor::corp_blue = SH110X_WHITE;

#ifdef DISPLAY_UTF8_FONTS
static bool hasNonASCII(const char *str)
{
  for (const char *p = str; *p; p++) {
    if ((unsigned char)*p >= 0x80) return true;
  }
  return false;
}
#endif

bool SH1106Display::begin()
{
  // Wire must already be initialised by board.begin() before this is called.
  // Boards with non-standard SH1106 addresses should define DISPLAY_ADDRESS
  // in their variant/platformio configuration. The SA0 strap selects 0x3C or
  // 0x3D and differs between revisions of the same board (e.g. T-Beam
  // Supreme), so fall back to the other address of the pair.
  uint8_t addr = 0;
  if (i2c_probe(Wire, DISPLAY_ADDRESS)) {
    addr = DISPLAY_ADDRESS;
  } else if (i2c_probe(Wire, DISPLAY_ADDRESS ^ 1)) {
    addr = DISPLAY_ADDRESS ^ 1;
  }
  // Run the Adafruit init even when no panel answered: it is what allocates
  // the frame buffer and the I2C device. Skipping it leaves i2c_dev and
  // spi_dev NULL, and UITask::begin() calls turnOn() regardless of our
  // return value, which then dereferences the null spi_dev.
  bool ok = display.begin(addr ? addr : DISPLAY_ADDRESS, true);
#ifdef DISPLAY_UTF8_FONTS
  _u8f.begin(display);
#endif
  return addr != 0 && ok;
}

void SH1106Display::turnOn()
{
  display.oled_command(SH110X_DISPLAYON);
  _isOn = true;
}

void SH1106Display::turnOff()
{
  display.oled_command(SH110X_DISPLAYOFF);
  _isOn = false;
}

void SH1106Display::clear()
{
  display.clearDisplay();
  display.display();
}

void SH1106Display::startFrame(ColorVal bkg)
{
  display.clearDisplay(); // TODO: apply 'bkg'
  _color = SH110X_WHITE;
  display.setTextColor(_color);
  setTextSize(1);
  display.cp437(true); // Use full 256 char 'Code Page 437' font
}

void SH1106Display::setTextSize(int sz)
{
  display.setTextSize(sz);
#ifdef DISPLAY_UTF8_FONTS
  // u8g2 only ships Cyrillic glyphs in a couple of fixed-size fonts; match
  // the closest one to the scaled built-in 5x7 GFX font (6px/8px line at
  // size 1, 12x16 at size 2+ -- 10x20 is the largest Cyrillic font available)
  if (sz <= 1) {
    _u8f.setFont(u8g2_font_6x12_t_cyrillic);
    _fontHeight = 12;
  } else {
    _u8f.setFont(u8g2_font_10x20_t_cyrillic);
    _fontHeight = 20;
  }
  _u8f.setFontMode(1);   // must follow setFont(): setFont() resets to solid mode, which paints each glyph's background box
#endif
}

void SH1106Display::setColor(ColorVal c)
{
  _color = c;
  display.setTextColor(_color);
}

void SH1106Display::setCursor(int x, int y)
{
  display.setCursor(x, y);
}

void SH1106Display::print(const char *str)
{
#ifdef DISPLAY_UTF8_FONTS
  if (hasNonASCII(str)) {
    printUTF8(str);
    return;
  }
#endif
  display.print(str);
}

#ifdef DISPLAY_UTF8_FONTS
// Renders str through the U8g2 overlay one UTF-8 character at a time, so it
// can wrap at the right edge the same way Adafruit_GFX::print() does for the
// ASCII path. u8g2 draws from the text baseline while Adafruit_GFX's cursor
// is top-left, so the cursor is offset by the font ascent going in and out.
void SH1106Display::printUTF8(const char *str)
{
  _u8f.setForegroundColor(_color);
  int16_t ascent = _u8f.getFontAscent();
  _u8f.setCursor(display.getCursorX(), display.getCursorY() + ascent);
  int16_t line_height = ascent - _u8f.getFontDescent() + 1;

  char glyph[5];
  for (const char *p = str; *p; ) {
    if (*p == '\n') {
      _u8f.setCursor(0, _u8f.getCursorY() + line_height);
      p++;
      continue;
    }
    int n = 1;
    while (n < 4 && (p[n] & 0xC0) == 0x80) n++;  // include UTF-8 continuation bytes
    memcpy(glyph, p, n);
    glyph[n] = 0;

    if (_u8f.getCursorX() + _u8f.getUTF8Width(glyph) > display.width()) {
      _u8f.setCursor(0, _u8f.getCursorY() + line_height);
    }
    _u8f.print(glyph);
    p += n;
  }
  display.setCursor(_u8f.getCursorX(), _u8f.getCursorY() - ascent);
}

// Greedy word wrap, measured and drawn entirely through the U8g2 overlay so
// wrap metrics stay consistent whether the text is ASCII, Cyrillic, or both.
// Splits on ASCII spaces only, which is UTF-8 safe since continuation bytes
// are always >= 0x80 and can never be mistaken for a space. A single word
// wider than max_width on its own is split mid-word at a clean UTF-8
// boundary instead of running off the edge.
//
// Only lines in [start_line, start_line + max_lines) are actually drawn, but
// every wrapped line is still counted -- the return value is always the
// *total* line count, letting callers (via printWordWrapScrolled) detect
// there's more content than fits and page/scroll through it.
int SH1106Display::wordWrapLines(const char *str, int max_width, int start_line, int max_lines)
{
  char line[256];
  size_t line_len = 0;
  int x0 = display.getCursorX();
  int16_t ascent = _u8f.getFontAscent();
  int y = display.getCursorY() + ascent;  // baseline for the first line
  int line_index = 0;
  const char *word_start = str;

  _u8f.setForegroundColor(_color);

  auto flush_line = [&]() {
    if (line_len > 0) {
      if (line_index >= start_line && line_index < start_line + max_lines) {
        line[line_len] = 0;
        _u8f.setCursor(x0, y);
        _u8f.print(line);
        y += _fontHeight;
      }
      line_index++;
      line_len = 0;
    }
  };

  while (true) {
    const char *word_end = word_start;
    while (*word_end && *word_end != ' ') word_end++;
    size_t word_len = word_end - word_start;
    if (word_len > sizeof(line) - 1) word_len = sizeof(line) - 1;

    if (word_len > 0) {
      char word_buf[256];
      memcpy(word_buf, word_start, word_len);
      word_buf[word_len] = 0;

      if ((int)_u8f.getUTF8Width(word_buf) > max_width) {
        // word alone is too wide: flush, then break it across as many
        // fresh lines as needed
        flush_line();
        const char *wp = word_start;
        size_t remaining = word_len;
        while (remaining > 0) {
          size_t take = remaining;
          char chunk[256];
          while (take > 0) {
            memcpy(chunk, wp, take);
            chunk[take] = 0;
            if ((int)_u8f.getUTF8Width(chunk) <= max_width) break;
            size_t shrink = take - 1;
            while (shrink > 0 && (((unsigned char)wp[shrink]) & 0xC0) == 0x80) shrink--;
            take = shrink;
          }
          if (take == 0) {
            // even one character doesn't fit (degenerate max_width); force
            // progress but keep it on a full UTF-8 boundary
            take = 1;
            while (take < remaining && (((unsigned char)wp[take]) & 0xC0) == 0x80) take++;
          }

          memcpy(line, wp, take);
          line_len = take;
          flush_line();

          wp += take;
          remaining -= take;
        }
      } else {
        char candidate[256];
        size_t candidate_len = line_len;
        memcpy(candidate, line, line_len);
        if (line_len > 0) candidate[candidate_len++] = ' ';
        memcpy(candidate + candidate_len, word_start, word_len);
        candidate_len += word_len;
        candidate[candidate_len] = 0;

        if (line_len > 0 && (int)_u8f.getUTF8Width(candidate) > max_width) {
          flush_line();
          memcpy(line, word_start, word_len);
          line_len = word_len;
        } else {
          memcpy(line, candidate, candidate_len);
          line_len = candidate_len;
        }
      }
    }

    if (*word_end == 0) break;
    word_start = word_end + 1;
  }

  flush_line();
  return line_index;
}

void SH1106Display::printWordWrap(const char *str, int max_width)
{
  wordWrapLines(str, max_width, 0, 1000);  // effectively unbounded line count
}

int SH1106Display::printWordWrapScrolled(const char *str, int max_width, int max_height, int start_line, int *out_visible_lines)
{
  int max_lines = max_height / _fontHeight;
  if (max_lines < 1) max_lines = 1;
  if (out_visible_lines) *out_visible_lines = max_lines;
  return wordWrapLines(str, max_width, start_line, max_lines);
}
#endif

void SH1106Display::fillRect(int x, int y, int w, int h)
{
  display.fillRect(x, y, w, h, _color);
}

void SH1106Display::drawRect(int x, int y, int w, int h)
{
  display.drawRect(x, y, w, h, _color);
}

void SH1106Display::drawXbm(int x, int y, const uint8_t *bits, int w, int h)
{
  display.drawBitmap(x, y, bits, w, h, _color);
}

uint16_t SH1106Display::getTextWidth(const char *str)
{
#ifdef DISPLAY_UTF8_FONTS
  if (hasNonASCII(str)) {
    return _u8f.getUTF8Width(str);
  }
#endif
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SH1106Display::endFrame()
{
  display.display();
}
