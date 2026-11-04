#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#define SH110X_NO_SPLASH
#include <Adafruit_SH110X.h>

#ifdef DISPLAY_UTF8_FONTS
  #include <U8g2_for_Adafruit_GFX.h>
#endif

#ifndef PIN_OLED_RESET
#define PIN_OLED_RESET -1
#endif

#ifndef DISPLAY_ADDRESS
#define DISPLAY_ADDRESS 0x3C
#endif

class SH1106Display : public DisplayDriver
{
  Adafruit_SH1106G display;
  bool _isOn;
  uint8_t _color;

  bool i2c_probe(TwoWire &wire, uint8_t addr);

#ifdef DISPLAY_UTF8_FONTS
  U8G2_FOR_ADAFRUIT_GFX _u8f;   // Unicode-capable overlay renderer (Cyrillic etc), layered on the Adafruit_GFX framebuffer
  uint8_t _fontHeight;
  void printUTF8(const char *str);
  // Word-wraps through _u8f so wrap metrics stay consistent for both ASCII and
  // non-ASCII text; draws only lines in [start_line, start_line + max_lines)
  // at the current cursor, but always returns the *total* wrapped line count.
  int wordWrapLines(const char *str, int max_width, int start_line, int max_lines);
#endif

public:
  SH1106Display() : DisplayDriver(128, 64), display(128, 64, &Wire, PIN_OLED_RESET) { _isOn = false; }
  bool begin();

  bool isOn() override { return _isOn; }
#ifdef DISPLAY_UTF8_FONTS
  bool supportsUTF8() override { return true; }
#endif
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char *str) override;
#ifdef DISPLAY_UTF8_FONTS
  void printWordWrap(const char *str, int max_width) override;
  int printWordWrapScrolled(const char *str, int max_width, int max_height, int start_line, int* out_visible_lines = nullptr) override;
#endif
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t *bits, int w, int h) override;
  uint16_t getTextWidth(const char *str) override;
  void endFrame() override;
};
