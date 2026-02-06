#include "WaveshareESP32C6LCDDisplay.h"

#include <Arduino.h>
#include <math.h>

static constexpr uint16_t COLOR_BLACK = 0x0000;
static constexpr uint16_t COLOR_WHITE = 0xFFFF;

WaveshareESP32C6LCDDisplay::WaveshareESP32C6LCDDisplay()
  : DisplayDriver(128, 64),
    _bus(new Arduino_HWSPI(15 /* DC */, 14 /* CS */, 7 /* SCK */, 6 /* MOSI */, 5 /* MISO */)),
    _display(new Arduino_ST7789(
      _bus,
      21 /* RST */,
      DISPLAY_ROTATION,
      true /* IPS */,
      TFT_WIDTH,
      TFT_HEIGHT,
      34 /* col offset 1 */,
      0 /* row offset 1 */,
      34 /* col offset 2 */,
      0 /* row offset 2 */)),
    _is_on(false),
    _color(COLOR_WHITE) {
}

int WaveshareESP32C6LCDDisplay::sx(int x) const {
  return (int)lroundf(x * DISPLAY_SCALE_X);
}

int WaveshareESP32C6LCDDisplay::sy(int y) const {
  return (int)lroundf(y * DISPLAY_SCALE_Y);
}

int WaveshareESP32C6LCDDisplay::sw(int w) const {
  int v = (int)lroundf(w * DISPLAY_SCALE_X);
  return (v < 1) ? 1 : v;
}

int WaveshareESP32C6LCDDisplay::sh(int h) const {
  int v = (int)lroundf(h * DISPLAY_SCALE_Y);
  return (v < 1) ? 1 : v;
}

bool WaveshareESP32C6LCDDisplay::begin() {
  if (_is_on) return true;

  // Disable the SD chip to avoid SPI bus contention with the LCD.
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);

  pinMode(22, OUTPUT);
  digitalWrite(22, HIGH);  // backlight on

  _display->begin(40000000);
  _display->fillScreen(COLOR_BLACK);
  _display->setTextColor(COLOR_WHITE);
  _display->setTextSize(1);
  _is_on = true;

  return true;
}

void WaveshareESP32C6LCDDisplay::turnOn() {
  if (!_is_on) begin();
  digitalWrite(22, HIGH);
  _is_on = true;
}

void WaveshareESP32C6LCDDisplay::turnOff() {
  digitalWrite(22, LOW);
  _is_on = false;
}

void WaveshareESP32C6LCDDisplay::clear() {
  _display->fillScreen(COLOR_BLACK);
}

void WaveshareESP32C6LCDDisplay::startFrame(Color bkg) {
  _display->fillScreen((bkg == DARK) ? COLOR_BLACK : COLOR_WHITE);
  _display->setTextColor((bkg == DARK) ? COLOR_WHITE : COLOR_BLACK);
}

void WaveshareESP32C6LCDDisplay::setTextSize(int sz) {
  int text_scale = (int)lroundf(sz * DISPLAY_SCALE_X);
  if (text_scale < 1) text_scale = 1;
  _display->setTextSize(text_scale);
}

void WaveshareESP32C6LCDDisplay::setColor(Color c) {
  switch (c) {
    case DARK:
      _color = COLOR_BLACK;
      break;
    case LIGHT:
      _color = COLOR_WHITE;
      break;
    case RED:
      _color = _display->color565(255, 0, 0);
      break;
    case GREEN:
      _color = _display->color565(0, 255, 0);
      break;
    case BLUE:
      _color = _display->color565(0, 0, 255);
      break;
    case YELLOW:
      _color = _display->color565(255, 255, 0);
      break;
    case ORANGE:
      _color = _display->color565(255, 165, 0);
      break;
    default:
      _color = COLOR_WHITE;
      break;
  }
  _display->setTextColor(_color);
}

void WaveshareESP32C6LCDDisplay::setCursor(int x, int y) {
  _display->setCursor(sx(x), sy(y));
}

void WaveshareESP32C6LCDDisplay::print(const char* str) {
  _display->print(str);
}

void WaveshareESP32C6LCDDisplay::fillRect(int x, int y, int w, int h) {
  _display->fillRect(sx(x), sy(y), sw(w), sh(h), _color);
}

void WaveshareESP32C6LCDDisplay::drawRect(int x, int y, int w, int h) {
  _display->drawRect(sx(x), sy(y), sw(w), sh(h), _color);
}

void WaveshareESP32C6LCDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  int pixel_w = sw(1);
  int pixel_h = sh(1);
  int base_x = sx(x);
  int base_y = sy(y);
  int byte_width = (w + 7) / 8;

  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint8_t byte = bits[row * byte_width + (col / 8)];
      if (byte & (0x80 >> (col & 7))) {
        _display->fillRect(base_x + col * pixel_w, base_y + row * pixel_h, pixel_w, pixel_h, _color);
      }
    }
  }
}

uint16_t WaveshareESP32C6LCDDisplay::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  _display->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return (uint16_t)lroundf(w / DISPLAY_SCALE_X);
}

void WaveshareESP32C6LCDDisplay::endFrame() {
  // Immediate mode display driver. Nothing to flush.
}
