#include "HeltecV3SSD1306Display.h"

bool HeltecV3SSD1306Display::i2c_probe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

bool HeltecV3SSD1306Display::begin() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();
    _isOn = true;
  }
  #ifdef DISPLAY_ROTATION
  display.setRotation(DISPLAY_ROTATION);
  #endif
  return display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, true, false) && i2c_probe(Wire, DISPLAY_ADDRESS);
}

void HeltecV3SSD1306Display::turnOn() {
  display.ssd1306_command(SSD1306_DISPLAYON);
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();
    _isOn = true;
  }
  // Re-enable charge pump and brightness after power cycling
  display.ssd1306_command(SSD1306_CHARGEPUMP);
  display.ssd1306_command(0x14);
  setBrightness(_brightness);
}

void HeltecV3SSD1306Display::turnOff() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  if (_isOn) {
    if (_peripher_power) _peripher_power->release();
    _isOn = false;
  }
}

void HeltecV3SSD1306Display::clear() {
  display.clearDisplay();
  display.display();
}

void HeltecV3SSD1306Display::startFrame(Color bkg) {
  display.clearDisplay();  // TODO: apply 'bkg'
  _color = SSD1306_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
}

void HeltecV3SSD1306Display::setTextSize(int sz) {
  display.setTextSize(sz);
}

void HeltecV3SSD1306Display::setColor(Color c) {
  _color = (c != 0) ? SSD1306_WHITE : SSD1306_BLACK;
  display.setTextColor(_color);
}

void HeltecV3SSD1306Display::setCursor(int x, int y) {
  display.setCursor(x, y);
}

void HeltecV3SSD1306Display::print(const char* str) {
  display.print(str);
}

void HeltecV3SSD1306Display::setBrightness(uint8_t brightness) {
  _brightness = brightness;

  uint8_t contrast = brightness;
  uint8_t precharge = (brightness == 0) ? 0x00 : 0xF1;
  // Slightly higher VCOMH can increase perceived brightness on some panels
  uint8_t comdetect = (brightness >= 200) ? 0x60 : 0x40;

  display.ssd1306_command(SSD1306_DISPLAYON);
  display.ssd1306_command(SSD1306_CHARGEPUMP);
  display.ssd1306_command(0x14);
  display.ssd1306_command(SSD1306_SETPRECHARGE);
  display.ssd1306_command(precharge);
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(contrast);
  display.ssd1306_command(SSD1306_SETVCOMDETECT);
  display.ssd1306_command(comdetect);
  display.ssd1306_command(SSD1306_DISPLAYALLON_RESUME);
  display.ssd1306_command(SSD1306_NORMALDISPLAY);
}

void HeltecV3SSD1306Display::fillRect(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, _color);
}

void HeltecV3SSD1306Display::drawRect(int x, int y, int w, int h) {
  display.drawRect(x, y, w, h, _color);
}

void HeltecV3SSD1306Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display.drawBitmap(x, y, bits, w, h, SSD1306_WHITE);
}

uint16_t HeltecV3SSD1306Display::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void HeltecV3SSD1306Display::endFrame() {
  display.display();
}
