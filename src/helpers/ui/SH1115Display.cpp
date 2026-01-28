#include "SH1115Display.h"
#include <Adafruit_GrayOLED.h>
//#include "Adafruit_SH110X.h"
#include <Adafruit_SH1115.h>

bool SH1115Display::i2c_probe(TwoWire &wire, uint8_t addr)
{
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

bool SH1115Display::begin()
{
  return display.begin(DISPLAY_ADDRESS, true) && i2c_probe(Wire, DISPLAY_ADDRESS);
}

void SH1115Display::turnOn()
{
  display.oled_command(SH110X_DISPLAYON);
  _isOn = true;
}

void SH1115Display::turnOff()
{
  display.oled_command(SH110X_DISPLAYOFF);
  _isOn = false;
}

void SH1115Display::clear()
{
  display.clearDisplay();
  display.display();
}

void SH1115Display::startFrame(Color bkg)
{
  display.clearDisplay(); // TODO: apply 'bkg'
  _color = SH110X_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true); // Use full 256 char 'Code Page 437' font
}

void SH1115Display::setTextSize(int sz)
{
  display.setTextSize(sz);
}

void SH1115Display::setColor(Color c)
{
  _color = (c != 0) ? SH110X_WHITE : SH110X_BLACK;
  display.setTextColor(_color);
}

void SH1115Display::setCursor(int x, int y)
{
  display.setCursor(x, y);
}

void SH1115Display::print(const char *str)
{
  display.print(str);
}

void SH1115Display::fillRect(int x, int y, int w, int h)
{
  display.fillRect(x, y, w, h, _color);
}

void SH1115Display::drawRect(int x, int y, int w, int h)
{
  display.drawRect(x, y, w, h, _color);
}

void SH1115Display::drawXbm(int x, int y, const uint8_t *bits, int w, int h)
{
  display.drawBitmap(x, y, bits, w, h, SH110X_WHITE);
}

uint16_t SH1115Display::getTextWidth(const char *str)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SH1115Display::endFrame()
{
  display.display();
}
