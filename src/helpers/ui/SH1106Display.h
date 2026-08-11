#pragma once

#include "DisplayDriver.h"
#include <U8g2lib.h>
#include <Wire.h>

#ifndef DISPLAY_ADDRESS
#define DISPLAY_ADDRESS 0x3C
#endif

class SH1106Display : public DisplayDriver
{
  U8G2_SH1106_128X64_NONAME_F_HW_I2C _u8g2;
  bool _isOn;
  uint8_t _drawColor;
  int _cursorX, _cursorY;

  bool i2c_probe(TwoWire &wire, uint8_t addr);
  void applyFont(int sz);

public:
  SH1106Display() : DisplayDriver(128, 64), _u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE),
      _isOn(false), _drawColor(1), _cursorX(0), _cursorY(0) {}
  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char *str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t *bits, int w, int h) override;
  uint16_t getTextWidth(const char *str) override;
  void endFrame() override;

  // The Cyrillic-capable U8g2 fonts used below can render the real UTF-8 text
  // directly, so skip the base class's default block-character fallback.
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override {
    size_t len = strlen(src);
    if (len >= dest_size) len = dest_size - 1;
    memcpy(dest, src, len);
    dest[len] = 0;
  }
};
