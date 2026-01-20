#pragma once

#include "DisplayDriver.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#define SSD1306_NO_SPLASH
#include <Adafruit_SSD1306.h>

#ifndef SSD1306_SPI_WIDTH
  #define SSD1306_SPI_WIDTH 64
#endif

#ifndef SSD1306_SPI_HEIGHT
  #define SSD1306_SPI_HEIGHT 48
#endif

class SSD1306SpiDisplay : public DisplayDriver {
  Adafruit_SSD1306 display;
  bool _isOn;
  uint8_t _color;

public:
  SSD1306SpiDisplay() : DisplayDriver(SSD1306_SPI_WIDTH, SSD1306_SPI_HEIGHT), 
    display(SSD1306_SPI_WIDTH, SSD1306_SPI_HEIGHT, &SPI, PIN_SPI_DISPLAY_DC, PIN_SPI_DISPLAY_RST, PIN_SPI_DISPLAY_CS) { 
    _isOn = false; 
  }

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
