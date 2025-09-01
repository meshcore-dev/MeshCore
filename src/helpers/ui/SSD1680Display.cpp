#include "SSD1680Display.h"

bool SSD1680Display::begin() {
  display.begin(true);
  return true;
}

// eInk displays are usually kept in deep sleep
// This means that drawing reinitializes them anyways.
void SSD1680Display::turnOn() {
}

void SSD1680Display::turnOff() {
}

void SSD1680Display::clear() {
  display.clearDisplay();
}

void SSD1680Display::startFrame(Color bkg) {
  display.clearBuffer();
  _color = EPD_BLACK;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
}

void SSD1680Display::setTextSize(int sz) {
  display.setTextSize(sz);
}

void SSD1680Display::setColor(Color c) {
  _color = (c != 0) ? EPD_BLACK : EPD_WHITE;
  display.setTextColor(_color);
}

void SSD1680Display::setCursor(int x, int y) {
  display.setCursor(x, y);
}

void SSD1680Display::print(const char* str) {
  display.print(str);
}

void SSD1680Display::fillRect(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, _color);
}

void SSD1680Display::drawRect(int x, int y, int w, int h) {
  display.drawRect(x, y, w, h, _color);
}

void SSD1680Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display.drawBitmap(x, y, bits, w, h, EPD_BLACK);
}

uint16_t SSD1680Display::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SSD1680Display::endFrame() {
  display.display();
}
