#pragma once

#include "DisplayDriver.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#define SSD1306_NO_SPLASH
#include <Adafruit_SSD1306.h>

class SSD1306SPIDisplay : public DisplayDriver {
  Adafruit_SSD1306 display;
  bool _isOn;
  bool _initialized;
  uint8_t _color;

  bool lazyInit();  // Deferred init for SPI bus sharing

public:
  // Accept pre-initialized SPI - do NOT call spi.begin()
  SSD1306SPIDisplay(SPIClass* spi, int16_t w, int16_t h, int8_t dc, int8_t rst, int8_t cs)
    : DisplayDriver(w, h), display(w, h, spi, dc, rst, cs) { _isOn = false; _initialized = false; }

  bool begin();

  bool isOn() override { return _isOn; }
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
