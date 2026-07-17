#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <SPI.h>
#include "TFT_eSPI.h"
#include <helpers/RefCountedDigitalPin.h>

#if defined(ESP32) && defined(FS_NO_GLOBALS)
// TFT_eSPI with SMOOTH_FONT defines FS_NO_GLOBALS before including FS.h,
// which suppresses the global aliases the rest of the codebase relies on.
#include <FS.h>
using fs::FS;
using fs::File;
using fs::SeekMode;
using fs::SeekSet;
using fs::SeekCur;
using fs::SeekEnd;
#endif

class ST7735Display : public DisplayDriver {
  bool _isOn;
  RefCountedDigitalPin* _peripher_power;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
public:
#ifdef USE_PIN_TFT
  ST7735Display(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64), 
    //  display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_SDA, PIN_TFT_SCL, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#else
  ST7735Display(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64),
    //  display(&SPI1, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
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
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;

protected:
  void _resetAndInit();
};
