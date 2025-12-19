#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "ST7789Spi.h"

class ST7789Display : public DisplayDriver {
  ST7789Spi display;
  bool _isOn;
  uint16_t _color;
  int _x=0, _y=0;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
public:
#ifdef HELTEC_VISION_MASTER_T190
  ST7789Display() : DisplayDriver(128, 64), display(&SPI, PIN_TFT_RST, PIN_TFT_DC, PIN_TFT_CS, GEOMETRY_RAWMODE, 320, 170,PIN_TFT_SDA,-1,PIN_TFT_SCL) {_isOn = false;}
#else
  ST7789Display() : DisplayDriver(128, 64), display(&SPI1, PIN_TFT_RST, PIN_TFT_DC, PIN_TFT_CS, GEOMETRY_RAWMODE, 240, 135) {_isOn = false;}
#endif
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
  void printWordWrap(const char* str, int max_width) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};
