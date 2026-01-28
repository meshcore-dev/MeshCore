#pragma once

#include "DisplayDriver.h"

#include <Adafruit_GFX.h>
#include <Wire.h>
#define SH110X_NO_SPLASH
// #include <Adafruit_SH110X.h>
#include <Adafruit_SH1115.h>

#ifndef PIN_OLED_RESET
#define PIN_OLED_RESET -1
#endif

#ifndef DISPLAY_ADDRESS
#define DISPLAY_ADDRESS 0x3C
#endif

class SH1115Display : public DisplayDriver {
  Adafruit_SH1115 display;
  bool _isOn;
  uint8_t _color;

  bool i2c_probe(TwoWire &wire, uint8_t addr);

public:
  SH1115Display() : DisplayDriver(128, 64), display(128, 64, &Wire, PIN_OLED_RESET) { _isOn = false; }
  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char *str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t *bits, int w, int h) override;
  uint16_t getTextWidth(const char *str) override;
  void endFrame() override;
};
