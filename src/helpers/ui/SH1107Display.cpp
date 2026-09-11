#include "SH1107Display.h"
#include <Adafruit_GrayOLED.h>
#include "Adafruit_SH110X.h"

#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 0
#endif

bool SH1107Display::i2c_probe(TwoWire &wire, uint8_t addr)
{
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

bool SH1107Display::begin()
{
  bool result = display.begin(DISPLAY_ADDRESS, true) && i2c_probe(Wire, DISPLAY_ADDRESS);
  if (result) {
    display.setRotation(DISPLAY_ROTATION);
  }
  return result;
}

void SH1107Display::turnOn()
{
  display.oled_command(SH110X_DISPLAYON);
  uint8_t cmd[] = {0xD5, 0xF0};
  display.oled_commandList(cmd, 2);
  _isOn = true;
}

void SH1107Display::turnOff()
{
  display.oled_command(SH110X_DISPLAYOFF);
  _isOn = false;
}

void SH1107Display::clear()
{
  display.clearDisplay();
  display.display();
}

void SH1107Display::startFrame(Color bkg)
{
  display.clearDisplay(); // TODO: apply 'bkg'
  display.setContrast(120); // 0-127. default setting was causing some flickering. 
  // display.SH110X_SETPRECHARGE(255);
  _color = SH110X_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true); // Use full 256 char 'Code Page 437' font
}

void SH1107Display::setTextSize(int sz)
{
  display.setTextSize(sz);
}

void SH1107Display::setColor(Color c)
{
  _color = (c != 0) ? SH110X_WHITE : SH110X_BLACK;
  display.setTextColor(_color);
}

void SH1107Display::setCursor(int x, int y)
{
  display.setCursor(x, y);
}

void SH1107Display::print(const char *str)
{
  display.print(str);
}

void SH1107Display::fillRect(int x, int y, int w, int h)
{
  display.fillRect(x, y, w, h, _color);
}

void SH1107Display::drawRect(int x, int y, int w, int h)
{
  display.drawRect(x, y, w, h, _color);
}

void SH1107Display::drawXbm(int x, int y, const uint8_t *bits, int w, int h)
{
  display.drawBitmap(x, y, bits, w, h, SH110X_WHITE);
}

uint16_t SH1107Display::getTextWidth(const char *str)
{
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SH1107Display::endFrame()
{
  display.display();
}
