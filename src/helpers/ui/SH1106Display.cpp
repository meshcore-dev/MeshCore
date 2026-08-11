#include "SH1106Display.h"

bool SH1106Display::i2c_probe(TwoWire &wire, uint8_t addr)
{
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

// Color scheme (monochrome: 0 = off, 1 = on)
ColorVal UIColor::window_bkg = 0;
ColorVal UIColor::title_bkg = 0;
ColorVal UIColor::title_txt = 1;
ColorVal UIColor::primary_txt = 1;
ColorVal UIColor::secondary_txt = 1;
ColorVal UIColor::warning_txt = 1;
ColorVal UIColor::popup_bkg = 0;
ColorVal UIColor::popup_txt = 1;
ColorVal UIColor::corp_blue = 1;

void SH1106Display::applyFont(int sz)
{
  // Both fonts are the X11 "fixed" family (public domain, ISO10646-encoded)
  // with full Basic Latin + Cyrillic glyph coverage, so message text and
  // node names render correctly regardless of script.
  if (sz >= 2) {
    _u8g2.setFont(u8g2_font_10x20_t_cyrillic);
  } else {
    _u8g2.setFont(u8g2_font_6x12_t_cyrillic);
  }
}

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
  _u8g2.setI2CAddress((addr ? addr : DISPLAY_ADDRESS) << 1);  // u8g2 wants 8-bit address
  bool ok = _u8g2.begin();
  if (ok) {
    _u8g2.setFontPosTop();  // y coordinate = top of text, not baseline
    _u8g2.setFontMode(1);   // transparent background
    applyFont(1);
  }
  return addr != 0 && ok;
}

void SH1106Display::turnOn()
{
  _u8g2.setPowerSave(0);
  _isOn = true;
}

void SH1106Display::turnOff()
{
  _u8g2.setPowerSave(1);
  _isOn = false;
}

void SH1106Display::clear()
{
  _u8g2.clearBuffer();
  _u8g2.sendBuffer();
}

void SH1106Display::startFrame(ColorVal bkg)
{
  _u8g2.clearBuffer(); // TODO: apply 'bkg'
  _drawColor = UIColor::primary_txt;
  _u8g2.setDrawColor(_drawColor);
  applyFont(1);
}

void SH1106Display::setTextSize(int sz)
{
  applyFont(sz);
}

void SH1106Display::setColor(ColorVal c)
{
  _drawColor = c;
  _u8g2.setDrawColor(_drawColor);
}

void SH1106Display::setCursor(int x, int y)
{
  _cursorX = x;
  _cursorY = y;
}

void SH1106Display::print(const char *str)
{
  _u8g2.drawUTF8(_cursorX, _cursorY, str);
}

void SH1106Display::fillRect(int x, int y, int w, int h)
{
  _u8g2.drawBox(x, y, w, h);
}

void SH1106Display::drawRect(int x, int y, int w, int h)
{
  _u8g2.drawFrame(x, y, w, h);
}

void SH1106Display::drawXbm(int x, int y, const uint8_t *bits, int w, int h)
{
  // Icon/logo data in this codebase is packed MSB-first per row (the
  // Adafruit_GFX::drawBitmap() convention), not U8g2's own LSB-first XBM
  // format, so use U8g2's u8glib-compatible drawBitmap() instead of
  // drawXBM() to avoid every byte's bits coming out mirrored/garbled.
  _u8g2.drawBitmap(x, y, (w + 7) / 8, h, bits);
}

uint16_t SH1106Display::getTextWidth(const char *str)
{
  return _u8g2.getUTF8Width(str);
}

void SH1106Display::endFrame()
{
  _u8g2.sendBuffer();
}
