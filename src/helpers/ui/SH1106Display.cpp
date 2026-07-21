#include "SH1106Display.h"
#include <Adafruit_GrayOLED.h>
#include "Adafruit_SH110X.h"

bool SH1106Display::i2c_probe(TwoWire &wire, uint8_t addr)
{
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

bool SH1106Display::begin()
{
  // Some T-Beam S3 Supreme revisions ship a QMC6310N magnetometer whose
  // configurable I2C address (0x3C / 0x3D) collides with the OLED's. Probe
  // both candidate addresses and disambiguate via the magnetometer's chip-ID
  // register: read of reg 0x00 returns 0x80 on the QMC6310N. Same trick
  // LilyGo's factory firmware uses.
  uint8_t candidates[] = { DISPLAY_ADDRESS, 0x3D };
  uint8_t found = 0;
  for (uint8_t addr : candidates) {
    if (!i2c_probe(Wire, addr)) continue;
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)0x00);
    if (Wire.endTransmission() != 0) continue;
    if (Wire.requestFrom((int)addr, (int)1) != 1) continue;
    if (Wire.read() == 0x80) continue;  // magnetometer, not the OLED
    found = addr;
    break;
  }
  if (found == 0) return false;
  return display.begin(found, true);
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

void SH1106Display::startFrame(Color bkg)
{
  display.clearDisplay(); // TODO: apply 'bkg'
  _color = SH110X_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true); // Use full 256 char 'Code Page 437' font
}

void SH1106Display::setTextSize(int sz)
{
  display.setTextSize(sz);
}

void SH1106Display::setColor(Color c)
{
  _color = (c != 0) ? SH110X_WHITE : SH110X_BLACK;
  display.setTextColor(_color);
}

void SH1106Display::setCursor(int x, int y)
{
  display.setCursor(x, y);
}

void SH1106Display::print(const char *str)
{
  display.print(str);
}

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
  display.drawBitmap(x, y, bits, w, h, SH110X_WHITE);
}

uint16_t SH1106Display::getTextWidth(const char *str)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SH1106Display::endFrame()
{
  display.display();
}
