#pragma once

#include "DisplayDriver.h"
#include <U8g2lib.h>
#include <Wire.h>

#ifndef DISPLAY_ADDRESS
  #define DISPLAY_ADDRESS   0x3C
#endif

#ifndef OLED_WIDTH
  #define OLED_WIDTH        72
#endif

#ifndef OLED_HEIGHT
  #define OLED_HEIGHT       40
#endif

class U8g2Display : public DisplayDriver {
  // U8g2 constructor for SSD1306/SSD1315 72×40 panel — handles all
  // GDDRAM column/page offsets, SETMULTIPLEX, SETDISPLAYOFFSET internally
  U8G2_SSD1306_72X40_ER_F_HW_I2C _u8g2;
  bool _isOn;
  uint8_t _drawColor;

  // Font metrics for current font (cached on setTextSize)
  uint8_t _fontAscent;
  uint8_t _fontHeight;

  void applyFont(int sz) {
    if (sz >= 2) {
      _u8g2.setFont(u8g2_font_5x7_mr);      // 5×7 — "large" for this display
    } else {
      _u8g2.setFont(u8g2_font_5x7_mr);
    }
    _fontAscent = _u8g2.getAscent();
    _fontHeight = _u8g2.getAscent() - _u8g2.getDescent();
  }

public:
  U8g2Display() : DisplayDriver(OLED_WIDTH, OLED_HEIGHT),
      _u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE),
      _isOn(false), _drawColor(1), _fontAscent(5), _fontHeight(6) {}

  bool begin() {
    // Wire must already be initialised by board.begin() before this is called
    bool ok = _u8g2.begin();
    if (ok) {
      _u8g2.setI2CAddress(DISPLAY_ADDRESS * 2);  // U8g2 uses 8-bit address
      _u8g2.setFontPosTop();     // y coordinate = top of text, not baseline
      _u8g2.setFontMode(1);      // transparent background
      applyFont(1);              // default to compact font
      _isOn = true;
    }
    return ok;
  }

  bool isOn() override { return _isOn; }

  void turnOn() override {
    _u8g2.setPowerSave(0);
    _isOn = true;
  }

  void turnOff() override {
    _u8g2.setPowerSave(1);
    _isOn = false;
  }

  void clear() override {
    _u8g2.clearBuffer();
    _u8g2.sendBuffer();
  }

  void startFrame(Color bkg = DARK) override {
    _u8g2.clearBuffer();
    _drawColor = 1;
    _u8g2.setDrawColor(1);
    applyFont(1);
  }

  void setTextSize(int sz) override {
    applyFont(sz);
  }

  void setColor(Color c) override {
    _drawColor = (c != DARK) ? 1 : 0;
    _u8g2.setDrawColor(_drawColor);
  }

  void setCursor(int x, int y) override {
    _cursorX = x;
    _cursorY = y;
  }

  void print(const char* str) override {
    _u8g2.setDrawColor(_drawColor);
    _u8g2.drawStr(_cursorX, _cursorY, str);
  }

  void fillRect(int x, int y, int w, int h) override {
    _u8g2.setDrawColor(_drawColor);
    _u8g2.drawBox(x, y, w, h);
  }

  void drawRect(int x, int y, int w, int h) override {
    _u8g2.setDrawColor(_drawColor);
    _u8g2.drawFrame(x, y, w, h);
  }

  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {
    _u8g2.setDrawColor(1);
    _u8g2.drawXBM(x, y, w, h, bits);
  }

  uint16_t getTextWidth(const char* str) override {
    return _u8g2.getStrWidth(str);
  }

  void endFrame() override {
    _u8g2.sendBuffer();
  }

private:
  int _cursorX = 0;
  int _cursorY = 0;
};
