#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#ifdef LILYGO_TLORA_PAGER
#include <Adafruit_ST7796S.h>
#else
#include <Adafruit_ST7789.h>
#endif
#if defined(LILYGO_TDECK) || defined(LILYGO_TLORA_PAGER)
#define MESHCORE_BUFFERED_SPI_DISPLAY 1
#endif
#include <helpers/RefCountedDigitalPin.h>

#ifdef MESHCORE_BUFFERED_SPI_DISPLAY
class TDeckCanvas;
#endif

class ST7789LCDDisplay : public DisplayDriver {
  #if defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT) || defined(HELTEC_V4_R8_TFT)
    SPIClass displaySPI;
  #endif
#ifdef LILYGO_TLORA_PAGER
  Adafruit_ST7796S display;
#else
  Adafruit_ST7789 display;
#endif
  bool _isOn;
#ifdef MESHCORE_BUFFERED_SPI_DISPLAY
  friend class TDeckCanvas;
  TDeckCanvas* _canvas = nullptr;
  bool _panel_ready = false;
  bool _buffered_rendering = true;
  bool _canvas_attempted = false;
  bool _frame_active = false;
  bool _render_callback_active = false;
  void (*_render_yield)() = nullptr;
  uint32_t _last_render_yield_us = 0;
  bool prepareCanvas();
  void serviceRender(bool force = false);
  void flushCanvas(int x, int y, int w, int h);
  void finishCanvasPrimitive(int x, int y, int w, int h);
#endif
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
#elif defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT) || defined(HELTEC_V4_R8_TFT)
  ST7789LCDDisplay(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64),
      displaySPI(HSPI),
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
#ifdef MESHCORE_BUFFERED_SPI_DISPLAY
  ~ST7789LCDDisplay();
  void setBufferedRendering(bool enabled);
  bool isBufferedRendering() const;
  // Runs with TFT CS released. Only service the radio; do not re-enter the UI.
  void setRenderYieldCallback(void (*callback)()) { _render_yield = callback; }
#endif

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
