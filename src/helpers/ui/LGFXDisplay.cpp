#include "LGFXDisplay.h"

// Color scheme
ColorVal UIColor::window_bkg = 0xFFFF;
ColorVal UIColor::title_bkg = 0x001F;
ColorVal UIColor::title_txt = 0xFFFF;
ColorVal UIColor::primary_txt = 0x0000;
ColorVal UIColor::secondary_txt = (18 << 11) | (36 << 5) | 18;  // mid-gray
ColorVal UIColor::warning_txt = 0xFD20;
ColorVal UIColor::popup_bkg =  0x07FF;  // CYAN
ColorVal UIColor::popup_txt = 0x0000;
ColorVal UIColor::corp_blue = 0x001A;

bool LGFXDisplay::begin() {
  turnOn();
  display->init();
  display->setRotation(1);
  display->setBrightness(64);
  display->setColorDepth(8);
  display->setTextColor(TFT_WHITE);

  buffer.setColorDepth(8);
  buffer.setPsram(true);
  buffer.createSprite(width(), height());

  return true;
}

void LGFXDisplay::turnOn() {
//  display->wakeup();
  if (!_isOn) {
    display->wakeup();
  }
  _isOn = true;
}

void LGFXDisplay::turnOff() {
  if (_isOn) {
    display->sleep();
  }
  _isOn = false;
}

void LGFXDisplay::clear() {
//  display->clearDisplay();
  buffer.clearDisplay();
}

void LGFXDisplay::startFrame(ColorVal bkg) {
//  display->startWrite();
//  display->getScanLine();
  _color = bkg;
  buffer.fillScreen(_color);
  buffer.setTextColor(_color = UIColor::primary_txt);
}

void LGFXDisplay::setTextSize(int sz) {
  buffer.setTextSize(sz);
}

void LGFXDisplay::setColor(ColorVal c) {
  buffer.setTextColor(_color = c);
}

void LGFXDisplay::setCursor(int x, int y) {
  buffer.setCursor(x, y);
}

void LGFXDisplay::print(const char* str) {
  buffer.println(str);
//  Serial.println(str);
}

void LGFXDisplay::fillRect(int x, int y, int w, int h) {
  buffer.fillRect(x, y, w, h, _color);
}

void LGFXDisplay::drawRect(int x, int y, int w, int h) {
  buffer.drawRect(x, y, w, h, _color);
}

void LGFXDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  buffer.drawBitmap(x, y, bits, w, h, _color);
}

uint16_t LGFXDisplay::getTextWidth(const char* str) {
  return buffer.textWidth(str);
}

void LGFXDisplay::endFrame() {
  display->startWrite();
  if (UI_ZOOM != 1) {
    buffer.pushRotateZoom(display, display->width()/2, display->height()/2 , 0, UI_ZOOM, UI_ZOOM);
  } else {
    buffer.pushSprite(display, 0, 0);
  }
  display->endWrite();
}

bool LGFXDisplay::getTouch(int *x, int *y) {
  lgfx::v1::touch_point_t point;
  display->getTouch(&point);
  if (UI_ZOOM != 1) {
    *x = point.x / UI_ZOOM;
    *y = point.y / UI_ZOOM;
  } else {
    *x = point.x;
    *y = point.y;
  }
  return (*x >= 0) && (*y >= 0);
}