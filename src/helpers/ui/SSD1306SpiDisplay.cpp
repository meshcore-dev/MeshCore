#include "SSD1306SpiDisplay.h"

bool SSD1306SpiDisplay::begin() {
#ifdef DISPLAY_ROTATION
  display.setRotation(DISPLAY_ROTATION);
#endif
  return display.begin(SSD1306_SWITCHCAPVCC);
}

void SSD1306SpiDisplay::turnOn() {
  display.ssd1306_command(SSD1306_DISPLAYON);
  _isOn = true;
}

void SSD1306SpiDisplay::turnOff() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  _isOn = false;
}

void SSD1306SpiDisplay::clear() {
  display.clearDisplay();
  display.display();
}

void SSD1306SpiDisplay::startFrame(Color bkg) {
  display.clearDisplay();
  _color = SSD1306_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true);
}

void SSD1306SpiDisplay::setTextSize(int sz) {
  display.setTextSize(sz);
}

void SSD1306SpiDisplay::setColor(Color c) {
  _color = (c != 0) ? SSD1306_WHITE : SSD1306_BLACK;
  display.setTextColor(_color);
}

void SSD1306SpiDisplay::setCursor(int x, int y) {
  display.setCursor(x, y);
}

void SSD1306SpiDisplay::print(const char* str) {
  display.print(str);
}

void SSD1306SpiDisplay::fillRect(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, _color);
}

void SSD1306SpiDisplay::drawRect(int x, int y, int w, int h) {
  display.drawRect(x, y, w, h, _color);
}

void SSD1306SpiDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display.drawBitmap(x, y, bits, w, h, SSD1306_WHITE);
}

uint16_t SSD1306SpiDisplay::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SSD1306SpiDisplay::endFrame() {
  display.display();
}
