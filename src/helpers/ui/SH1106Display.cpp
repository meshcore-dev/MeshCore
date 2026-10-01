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
  display.setTextSize(1);
  display.cp437(true); // Use full 256 char 'Code Page 437' font
}

void SH1106Display::setTextSize(int sz)
{
  display.setTextSize(sz);
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
  display.drawBitmap(x, y, bits, w, h, _color);
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
