#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#define SSD1306_NO_SPLASH
#include <Adafruit_SSD1306.h>
#ifdef USE_SPI_SSD1306 // Note: current display implementation shares the same SPI bus as the radio.
  #include <SPI.h>
  #if defined(DISPLAY_SPI) && (DISPLAY_SPI == 1)
    #undef DISPLAY_SPI
    #define DISPLAY_SPI spi
  #endif
  #ifndef DISPLAY_SPI
    #define DISPLAY_SPI SPI
  #endif
  extern SPIClass DISPLAY_SPI;
#endif

#ifndef PIN_OLED_RESET
  #define PIN_OLED_RESET        21 // Reset pin # (or -1 if sharing Arduino reset pin)
#endif

#ifdef USE_SPI_SSD1306
  #ifndef PIN_OLED_CS
    #define PIN_OLED_CS        5
  #endif
  #ifndef PIN_OLED_DC
    #define PIN_OLED_DC        4
  #endif
  #ifndef PIN_OLED_SCK
    #define PIN_OLED_SCK       SCK
  #endif
  #ifndef PIN_OLED_MISO
    #ifdef P_LORA_MISO
      #define PIN_OLED_MISO    P_LORA_MISO
    #else
      #define PIN_OLED_MISO    MISO
    #endif
  #endif
  #ifndef PIN_OLED_MOSI
    #define PIN_OLED_MOSI      MOSI
  #endif
#endif

#ifndef DISPLAY_ADDRESS
  #define DISPLAY_ADDRESS   0x3C
#endif

class SSD1306Display : public DisplayDriver {
  Adafruit_SSD1306 display;
  bool _isOn;
  uint8_t _color;

#ifndef USE_SPI_SSD1306
  bool i2c_probe(TwoWire& wire, uint8_t addr);
#endif
public:
#ifdef USE_SPI_SSD1306
  SSD1306Display() : DisplayDriver(128, 64), display(128, 64, &DISPLAY_SPI, PIN_OLED_DC, PIN_OLED_RESET, PIN_OLED_CS) { _isOn = false; }
#else
  SSD1306Display() : DisplayDriver(128, 64), display(128, 64, &Wire, PIN_OLED_RESET) { _isOn = false; }
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
};
