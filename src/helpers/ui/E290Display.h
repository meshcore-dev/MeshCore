#pragma once

#include "DisplayDriver.h"

#include <SPI.h>
#include <Wire.h>
#include <heltec-eink-modules.h>

// Display driver for E290 e-ink display
class E290Display : public DisplayDriver {
  EInkDisplay_VisionMasterE290 display;
  bool _init = false;
  bool _isOn = false;

public:
  E290Display() : DisplayDriver(296, 128) {}

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

private:
  void powerOn();
  void powerOff();
};