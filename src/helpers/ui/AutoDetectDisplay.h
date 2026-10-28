#pragma once

#include "DisplayDriver.h"
#include "SSD1306Display.h"
#include "SH1106Display.h"

// Auto detects SH1106 display at runtime, otherwise falls back to using SSD1306
class AutoDetectDisplay : public DisplayDriver {
  DisplayDriver* _impl;
  SSD1306Display _ssd1306;
  SH1106Display _sh1106;

  int getDisplayStatus(TwoWire& wire, uint8_t addr);
public:
  AutoDetectDisplay() : DisplayDriver(128, 64), _impl(&_ssd1306) { }
  bool begin();

  bool isOn() override { return _impl->isOn(); }
  void turnOn() override { _impl->turnOn(); }
  void turnOff() override { _impl->turnOff(); }
  void clear() override { _impl->clear(); }
  void startFrame(ColorVal bkg = UIColor::window_bkg) override { _impl->startFrame(bkg); }
  void setTextSize(int sz) override { _impl->setTextSize(sz); }
  void setColor(ColorVal c) override { _impl->setColor(c); }
  void setCursor(int x, int y) override { _impl->setCursor(x, y); }
  void print(const char* str) override { _impl->print(str); }
  void fillRect(int x, int y, int w, int h) override { _impl->fillRect(x, y, w, h); }
  void drawRect(int x, int y, int w, int h) override { _impl->drawRect(x, y, w, h); }
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override { _impl->drawXbm(x, y, bits, w, h); }
  uint16_t getTextWidth(const char* str) override { return _impl->getTextWidth(str); }
  void endFrame() override { _impl->endFrame(); }
};
