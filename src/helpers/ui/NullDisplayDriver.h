#pragma once

#include <helpers/ui/DisplayDriver.h>

class NullDisplayDriver : public DisplayDriver {
public:
  NullDisplayDriver() : DisplayDriver(128, 64) { }
  bool begin() { return false; }   // not present

  bool isOn() override { return false; }
  void turnOn() override { }
  void turnOff() override { }
  void clear() override { }
  void startFrame(Color bkg = DARK) override { }
  void setTextSize(int sz) override { }
  void setColor(Color c) override { }
  void setCursor(int x, int y) override { }
  void print(const char* str) override { }
  void fillRect(int x, int y, int w, int h) override { }
  void drawRect(int x, int y, int w, int h) override { }
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override { }
  uint16_t getTextWidth(const char* str) override { return 0; }
  void endFrame() { }
};
