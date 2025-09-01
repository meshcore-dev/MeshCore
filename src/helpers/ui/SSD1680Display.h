#pragma once

#include "DisplayDriver.h"
#include <drivers/Adafruit_SSD1680.h>


class SSD1680Display : public DisplayDriver {
  Adafruit_SSD1680 display;
  bool _display_on;
  uint32_t _last_full_update;
  uint8_t _partial_update_count;
  uint8_t _color;

public:
  SSD1680Display() : DisplayDriver(250, 122), display(250, 122, PIN_DISPLAY_DC, PIN_DISPLAY_RST, PIN_DISPLAY_CS, -1, PIN_DISPLAY_BUSY, &SPI1) { _display_on = false; _last_full_update = 0; _partial_update_count = -1;};
  bool begin();

  bool isOn() override { return true; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};
