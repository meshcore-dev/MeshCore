#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <Arduino_GFX_Library.h>

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

#ifndef DISPLAY_SCALE_X
  #define DISPLAY_SCALE_X 1.34375f  // 172 / 128
#endif

#ifndef DISPLAY_SCALE_Y
  #define DISPLAY_SCALE_Y 5.0f      // 320 / 64
#endif

class WaveshareESP32C6LCDDisplay : public DisplayDriver {
  Arduino_DataBus* _bus;
  Arduino_GFX* _display;
  bool _is_on;
  uint16_t _color;

  static constexpr int TFT_WIDTH = 172;
  static constexpr int TFT_HEIGHT = 320;

  int sx(int x) const;
  int sy(int y) const;
  int sw(int w) const;
  int sh(int h) const;

public:
  WaveshareESP32C6LCDDisplay();

  bool begin();

  bool isOn() override { return _is_on; }
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
