#include "GxEPD2_154_Z91c.h"

GxEPD2_154_Z91c::GxEPD2_154_Z91c(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
  : GxEPD2_EPD(cs, dc, rst, busy, HIGH, 30000000, WIDTH, HEIGHT, panel,
               hasColor, hasPartialUpdate, hasFastPartialUpdate) {}

void GxEPD2_154_Z91c::initDisplay() {
  if (_hibernating) _reset();
  delay(10);
  _writeCommand(0x12); // software reset
  delay(10);
  _writeCommand(0x01); // driver output: 152 gates
  _writeData(0x97);
  _writeData(0x00);
  _writeData(0x00);
  _writeCommand(0x11); // X increment, then Y increment
  _writeData(0x03);
  _writeCommand(0x3C); // border follows LUT
  _writeData(0x05);
  _writeCommand(0x18); // use internal temperature sensor
  _writeData(0x80);
  _writeCommand(0x21); // black normal, red RAM inverted by write path
  _writeData(0x00);
  _writeData(0x80);
  setRamArea(0, 0, WIDTH, HEIGHT);
}

void GxEPD2_154_Z91c::setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  _writeCommand(0x44);
  _writeData(x / 8);
  _writeData((x + w - 1) / 8);
  _writeCommand(0x45);
  _writeData(y & 0xFF);
  _writeData(y >> 8);
  _writeData((y + h - 1) & 0xFF);
  _writeData((y + h - 1) >> 8);
  _writeCommand(0x4E);
  _writeData(x / 8);
  _writeCommand(0x4F);
  _writeData(y & 0xFF);
  _writeData(y >> 8);
}

void GxEPD2_154_Z91c::writeScreenBuffer(uint8_t value) {
  writeScreenBuffer(value, 0xFF);
}

void GxEPD2_154_Z91c::writeScreenBuffer(uint8_t black_value, uint8_t color_value) {
  _initial_write = false;
  initDisplay();
  _writeCommand(0x24);
  for (uint32_t i = 0; i < uint32_t(WIDTH) * HEIGHT / 8; i++) _writeData(black_value);
  _writeCommand(0x26);
  for (uint32_t i = 0; i < uint32_t(WIDTH) * HEIGHT / 8; i++) _writeData(~color_value);
}

void GxEPD2_154_Z91c::clearScreen(uint8_t value) {
  clearScreen(value, 0xFF);
}

void GxEPD2_154_Z91c::clearScreen(uint8_t black_value, uint8_t color_value) {
  writeScreenBuffer(black_value, color_value);
  update();
}

void GxEPD2_154_Z91c::writeImage(const uint8_t bitmap[], int16_t x, int16_t y,
                                 int16_t w, int16_t h, bool invert,
                                 bool mirror_y, bool pgm) {
  writeImage(bitmap, NULL, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_Z91c::writeImage(const uint8_t* black, const uint8_t* color,
                                 int16_t x, int16_t y, int16_t w, int16_t h,
                                 bool invert, bool mirror_y, bool pgm) {
  if (_initial_write) writeScreenBuffer();
  int16_t wb = (w + 7) / 8;
  x -= x % 8;
  w = wb * 8;
  int16_t x1 = max<int16_t>(0, x);
  int16_t y1 = max<int16_t>(0, y);
  int16_t w1 = min<int16_t>(WIDTH, x + w) - x1;
  int16_t h1 = min<int16_t>(HEIGHT, y + h) - y1;
  if (w1 <= 0 || h1 <= 0) return;
  w1 -= w1 % 8;
  if (w1 <= 0) return;
  initDisplay();
  setRamArea(x1, y1, w1, h1);

  const uint8_t* planes[2] = { black, color };
  const uint8_t commands[2] = { 0x24, 0x26 };
  for (uint8_t plane = 0; plane < 2; plane++) {
    _writeCommand(commands[plane]);
    for (int16_t row = 0; row < h1; row++) {
      for (int16_t col = 0; col < w1 / 8; col++) {
        uint8_t data = 0xFF;
        if (planes[plane]) {
          int16_t source_y = mirror_y ? h - 1 - (row + y1 - y) : row + y1 - y;
          int16_t idx = col + (x1 - x) / 8 + source_y * wb;
          data = pgm ? pgm_read_byte(planes[plane] + idx) : planes[plane][idx];
          if (invert) data = ~data;
        }
        _writeData(plane == 1 ? ~data : data);
      }
    }
  }
  delay(1);
}

void GxEPD2_154_Z91c::writeImagePart(
    const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap,
    int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h,
    bool invert, bool mirror_y, bool pgm) {
  writeImagePart(bitmap, NULL, x_part, y_part, w_bitmap, h_bitmap,
                 x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_Z91c::writeImagePart(
    const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part,
    int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y, int16_t w,
    int16_t h, bool invert, bool mirror_y, bool pgm) {
  if (x_part == 0 && y_part == 0 && w == w_bitmap && h == h_bitmap) {
    writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
    return;
  }
  // GxEPD2's full-buffer path used by MeshCore always enters the branch above.
  // Reject unsupported sliced writes instead of addressing outside panel RAM.
}

void GxEPD2_154_Z91c::writeNative(const uint8_t* data1, const uint8_t* data2,
                                  int16_t x, int16_t y, int16_t w, int16_t h,
                                  bool invert, bool mirror_y, bool pgm) {
  writeImage(data1, data2, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_Z91c::drawImage(const uint8_t bitmap[], int16_t x, int16_t y,
                                int16_t w, int16_t h, bool invert,
                                bool mirror_y, bool pgm) {
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh();
}

void GxEPD2_154_Z91c::drawImagePart(
    const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap,
    int16_t h_bitmap, int16_t x, int16_t y, int16_t w, int16_t h,
    bool invert, bool mirror_y, bool pgm) {
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap,
                 x, y, w, h, invert, mirror_y, pgm);
  refresh();
}

void GxEPD2_154_Z91c::drawImage(const uint8_t* black, const uint8_t* color,
                                int16_t x, int16_t y, int16_t w, int16_t h,
                                bool invert, bool mirror_y, bool pgm) {
  writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
  refresh();
}

void GxEPD2_154_Z91c::drawImagePart(
    const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part,
    int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y, int16_t w,
    int16_t h, bool invert, bool mirror_y, bool pgm) {
  writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap,
                 x, y, w, h, invert, mirror_y, pgm);
  refresh();
}

void GxEPD2_154_Z91c::drawNative(const uint8_t* data1, const uint8_t* data2,
                                 int16_t x, int16_t y, int16_t w, int16_t h,
                                 bool invert, bool mirror_y, bool pgm) {
  drawImage(data1, data2, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_154_Z91c::update() {
  _writeCommand(0x22);
  _writeData(0xF7);
  _writeCommand(0x20);
  _waitWhileBusy("GDEM0154Z91", full_refresh_time);
  _power_is_on = false;
}

void GxEPD2_154_Z91c::refresh(bool) {
  update();
}

void GxEPD2_154_Z91c::refresh(int16_t, int16_t, int16_t, int16_t) {
  update();
}

void GxEPD2_154_Z91c::powerOff() {
  _writeCommand(0x22);
  _writeData(0x83);
  _writeCommand(0x20);
  _waitWhileBusy("powerOff", power_off_time);
  _power_is_on = false;
}

void GxEPD2_154_Z91c::hibernate() {
  powerOff();
  if (_rst >= 0) {
    _writeCommand(0x10);
    _writeData(0x01);
    _hibernating = true;
  }
}
