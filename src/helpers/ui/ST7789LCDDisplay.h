#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <helpers/RefCountedDigitalPin.h>

#ifdef LILKA_BOARD
// Adafruit_ST77xx::setColRowStart() is protected, so it can't be called on a
// plain Adafruit_ST7789 instance from outside the class. This exposes it so the
// Lilka's 280x240 panel column/row offset can be applied.
class LilkaST7789 : public Adafruit_ST7789 {
public:
  using Adafruit_ST7789::Adafruit_ST7789;
  void setPanelOffset(int8_t col, int8_t row) { setColRowStart(col, row); }
};
#endif

class ST7789LCDDisplay : public DisplayDriver {
  #if defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT) || defined(HELTEC_V4_R8_TFT) || defined(LILKA_BOARD)
    SPIClass displaySPI;
  #endif
  #ifdef LILKA_BOARD
    LilkaST7789 display;
  #else
    Adafruit_ST7789 display;
  #endif
  bool _isOn;
  uint16_t _color;
  RefCountedDigitalPin* _peripher_power;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
public:
#ifdef USE_PIN_TFT
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64), 
      display(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_SDA, PIN_TFT_SCL, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#elif defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT) || defined(HELTEC_V4_R8_TFT) || defined(LILKA_BOARD)
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64),
    #ifdef LILKA_BOARD
      displaySPI(FSPI),   // HSPI is used by the LoRa radio on Lilka, so the display uses FSPI
    #else
      displaySPI(HSPI),
    #endif
      display(&displaySPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST),
      _peripher_power(peripher_power)
  {
    _isOn = false;
  }
#else
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64), 
      display(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST),
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
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};
