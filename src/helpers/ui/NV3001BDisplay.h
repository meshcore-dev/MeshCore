#pragma once

#include "DisplayDriver.h"
#include <SPI.h>
#include <helpers/RefCountedDigitalPin.h>

#ifndef NV3001B_LOGICAL_WIDTH
  #define NV3001B_LOGICAL_WIDTH 128
#endif

#ifndef NV3001B_LOGICAL_HEIGHT
  #define NV3001B_LOGICAL_HEIGHT 64
#endif

#ifndef NV3001B_PANEL_WIDTH
  #define NV3001B_PANEL_WIDTH 128
#endif

#ifndef NV3001B_PANEL_HEIGHT
  #define NV3001B_PANEL_HEIGHT 220
#endif

#ifndef NV3001B_SPI_HOST
  #define NV3001B_SPI_HOST HSPI
#endif

class NV3001BDisplay : public DisplayDriver {
  SPIClass spi;
  RefCountedDigitalPin* periph_power;
  bool is_on = false;
  uint16_t color = 0xffff;
  uint8_t text_size = 1;
  int cursor_x = 0;
  int cursor_y = 0;

  void writeCommand(uint8_t cmd);
  void writeBytes(const uint8_t* data, size_t len);
  void writeCommandData(uint8_t cmd, const uint8_t* data, size_t len);
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writeColor(uint16_t rgb, uint32_t count);
  void fillPhysicalRect(int x, int y, int w, int h);
  void initPanel();
  void drawChar(int x, int y, char ch);

public:
  NV3001BDisplay(RefCountedDigitalPin* power = nullptr) :
      DisplayDriver(NV3001B_LOGICAL_WIDTH, NV3001B_LOGICAL_HEIGHT), spi(NV3001B_SPI_HOST), periph_power(power) { }

  bool begin();
  static const char* driverName() { return "NV3001B"; }
  static uint16_t physicalWidth() { return NV3001B_PANEL_WIDTH; }
  static uint16_t physicalHeight() { return NV3001B_PANEL_HEIGHT; }

  bool isOn() override { return is_on; }
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
