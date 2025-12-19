#include "ST7735Display.h"

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 2
#endif

#define SCALE_X  1.25f     // 160 / 128
#define SCALE_Y  1.25f      // 80 / 64

bool ST7735Display::i2c_probe(TwoWire& wire, uint8_t addr) {
  return true;
/*
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
*/
}

bool ST7735Display::begin() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();

    pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
    digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
    digitalWrite(PIN_TFT_RST, HIGH);

#if defined(HELTEC_TRACKER_V2)
    display.initR(INITR_MINI160x80);
    display.setRotation(DISPLAY_ROTATION);
    uint8_t madctl = ST77XX_MADCTL_MY | ST77XX_MADCTL_MV |ST7735_MADCTL_BGR;//Adjust color to BGR
    display.sendCommand(ST77XX_MADCTL, &madctl, 1);
#else
    display.initR(INITR_MINI160x80_PLUGIN);
    display.setRotation(DISPLAY_ROTATION);
#endif
    display.setSPISpeed(40000000);
    display.fillScreen(ST77XX_BLACK);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2); 
    display.cp437(true);         // Use full 256 char 'Code Page 437' font
    
    _isOn = true;
  }
  return true;
}

void ST7735Display::turnOn() {
  ST7735Display::begin();
}

void ST7735Display::turnOff() {
  if (_isOn) {
    digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
    digitalWrite(PIN_TFT_RST, LOW);
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);
    _isOn = false;

    if (_peripher_power) _peripher_power->release();
  }
}

void ST7735Display::clear() {
  //Serial.println("DBG: display.Clear");
  display.fillScreen(ST77XX_BLACK);
}

void ST7735Display::startFrame(Color bkg) {
  display.fillScreen(0x00);
  display.setTextColor(ST77XX_WHITE);
  display.setTextSize(1);      // This one affects size of Please wait... message
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
}

void ST7735Display::setTextSize(int sz) {
  display.setTextSize(sz);
}

void ST7735Display::setColor(Color c) {
  switch (c) {
    case DisplayDriver::DARK :
      _color = ST77XX_BLACK;
      break;
    case DisplayDriver::LIGHT : 
      _color = ST77XX_WHITE;
      break;
    case DisplayDriver::RED : 
      _color = ST77XX_RED;
      break;
    case DisplayDriver::GREEN : 
      _color = ST77XX_GREEN;
      break;
    case DisplayDriver::BLUE : 
      _color = ST77XX_BLUE;
      break;
    case DisplayDriver::YELLOW : 
      _color = ST77XX_YELLOW;
      break;
    case DisplayDriver::ORANGE : 
      _color = ST77XX_ORANGE;
      break;
    default:
      _color = ST77XX_WHITE;
      break;
  }
  display.setTextColor(_color);
}

void ST7735Display::setCursor(int x, int y) {
  display.setCursor(x*SCALE_X, y*SCALE_Y);
}

void ST7735Display::print(const char* str) {
  display.print(str);
}

void ST7735Display::fillRect(int x, int y, int w, int h) {
  display.fillRect(x*SCALE_X, y*SCALE_Y, w*SCALE_X, h*SCALE_Y, _color);
}

void ST7735Display::drawRect(int x, int y, int w, int h) {
  display.drawRect(x*SCALE_X, y*SCALE_Y, w*SCALE_X, h*SCALE_Y, _color);
}

void ST7735Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display.drawBitmap(x*SCALE_X, y*SCALE_Y, bits, w, h, _color);
}

uint16_t ST7735Display::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w / SCALE_X;
}

void ST7735Display::endFrame() {
  // display.display();
}
