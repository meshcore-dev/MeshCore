#include "ST7789LCDDisplay.h"

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 3
#endif

#ifndef DISPLAY_SCALE_X
  #define DISPLAY_SCALE_X 2.5f // 320 / 128
#endif

#ifndef DISPLAY_SCALE_Y
  #define DISPLAY_SCALE_Y 3.75f // 240 / 64
#endif

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

bool ST7789LCDDisplay::i2c_probe(TwoWire& wire, uint8_t addr) {
  return true;
}

bool ST7789LCDDisplay::begin() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();

    if (PIN_TFT_LEDA_CTL != -1) {
      pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
      digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
    }
    if (PIN_TFT_RST != -1) {
      pinMode(PIN_TFT_RST, OUTPUT);
      digitalWrite(PIN_TFT_RST, LOW); 
      delay(10);
      digitalWrite(PIN_TFT_RST, HIGH);
    }

    // Im not sure if this is just a t-deck problem or not, if your display is slow try this.
    #if defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT)
      displaySPI.begin(PIN_TFT_SCL, -1, PIN_TFT_SDA, PIN_TFT_CS);
    #endif

    display.init(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    display.setRotation(DISPLAY_ROTATION);

    display.setSPISpeed(40e6);

    display.fillScreen(ST77XX_BLACK);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2 * DISPLAY_SCALE_X); 
    display.cp437(true); // Use full 256 char 'Code Page 437' font
  
    _isOn = true;
  }

  return true;
}

void ST7789LCDDisplay::turnOn() {
  ST7789LCDDisplay::begin();
}

void ST7789LCDDisplay::turnOff() {
  if (_isOn) {
    if (PIN_TFT_LEDA_CTL != -1) {
      digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
    }
    if (PIN_TFT_RST != -1) {
      digitalWrite(PIN_TFT_RST, LOW);
    }
    if (PIN_TFT_LEDA_CTL != -1) {
      digitalWrite(PIN_TFT_LEDA_CTL, LOW);
    }
    _isOn = false;

    if (_peripher_power) _peripher_power->release();
  }
}

void ST7789LCDDisplay::clear() {
  display.fillScreen(ST77XX_BLACK);
}

void ST7789LCDDisplay::startFrame(Color bkg) {
  display.fillScreen(ST77XX_BLACK);
  display.setTextColor(ST77XX_WHITE);
  display.setTextSize(1 * DISPLAY_SCALE_X); // This one affects size of Please wait... message
  display.cp437(true); // Use full 256 char 'Code Page 437' font
}

void ST7789LCDDisplay::setTextSize(int sz) {
  display.setTextSize(sz * DISPLAY_SCALE_X);
}

void ST7789LCDDisplay::setColor(Color c) {
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

void ST7789LCDDisplay::setCursor(int x, int y) {
  display.setCursor(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y);
}

void ST7789LCDDisplay::print(const char* str) {
  display.print(str);
}

void ST7789LCDDisplay::fillRect(int x, int y, int w, int h) {
  display.fillRect(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y, w * DISPLAY_SCALE_X, h * DISPLAY_SCALE_Y, _color);
}

void ST7789LCDDisplay::drawRect(int x, int y, int w, int h) {
  display.drawRect(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y, w * DISPLAY_SCALE_X, h * DISPLAY_SCALE_Y, _color);
}

void ST7789LCDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  uint8_t byteWidth = (w + 7) / 8;

  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      uint8_t byte = bits[j * byteWidth + i / 8];
      bool pixelOn = byte & (0x80 >> (i & 7));

      if (pixelOn) {
        for (int dy = 0; dy < DISPLAY_SCALE_X; dy++) {
          for (int dx = 0; dx < DISPLAY_SCALE_X; dx++) {
            display.drawPixel(x * DISPLAY_SCALE_X + i * DISPLAY_SCALE_X + dx, y * DISPLAY_SCALE_Y + j * DISPLAY_SCALE_X + dy, _color);
          }
        }
      }
    }
  }
}

uint16_t ST7789LCDDisplay::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);

  return w / DISPLAY_SCALE_X;
}

void ST7789LCDDisplay::endFrame() {
  // display.display();
}