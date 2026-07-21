#include "NV3001BDisplay.h"
#include <Arduino.h>
#include <string.h>

#ifndef SPI_FREQUENCY
  #define SPI_FREQUENCY 8000000
#endif

#ifndef PIN_TFT_SCL
  #error "PIN_TFT_SCL must be defined"
#endif

#ifndef PIN_TFT_SDA
  #error "PIN_TFT_SDA must be defined"
#endif

#ifndef PIN_TFT_CS
  #error "PIN_TFT_CS must be defined"
#endif

#ifndef PIN_TFT_DC
  #error "PIN_TFT_DC must be defined"
#endif

#ifndef PIN_TFT_MISO
  #define PIN_TFT_MISO -1
#endif

#ifndef PIN_TFT_RST
  #define PIN_TFT_RST -1
#endif

#ifndef PIN_TFT_EN
  #define PIN_TFT_EN -1
#endif

#ifndef PIN_TFT_BL
  #define PIN_TFT_BL -1
#endif

#ifndef PIN_TFT_EN_ACTIVE
  #define PIN_TFT_EN_ACTIVE LOW
#endif

#ifndef PIN_TFT_BL_ACTIVE
  #define PIN_TFT_BL_ACTIVE HIGH
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

#ifndef NV3001B_SCREEN_WIDTH
  #define NV3001B_SCREEN_WIDTH 220
#endif

#ifndef NV3001B_SCREEN_HEIGHT
  #define NV3001B_SCREEN_HEIGHT 128
#endif

#ifndef DISPLAY_SCALE_X
  #define DISPLAY_SCALE_X ((float)NV3001B_SCREEN_WIDTH / NV3001B_LOGICAL_WIDTH)
#endif

#ifndef DISPLAY_SCALE_Y
  #define DISPLAY_SCALE_Y ((float)NV3001B_SCREEN_HEIGHT / NV3001B_LOGICAL_HEIGHT)
#endif

#define NV3001B_SWRESET 0x01
#define NV3001B_SLPOUT  0x11
#define NV3001B_DISPON  0x29
#define NV3001B_CASET   0x2A
#define NV3001B_RASET   0x2B
#define NV3001B_RAMWR   0x2C
#define NV3001B_MADCTL  0x36
#define NV3001B_COLMOD  0x3A

#define NV3001B_MADCTL_MY  0x80
#define NV3001B_MADCTL_MX  0x40
#define NV3001B_MADCTL_MV  0x20
#define NV3001B_MADCTL_RGB 0x00

#ifndef NV3001B_TEXT_SIZE1_SCALE_X
  #define NV3001B_TEXT_SIZE1_SCALE_X 1
#endif

#ifndef NV3001B_TEXT_SIZE1_SCALE_Y
  #define NV3001B_TEXT_SIZE1_SCALE_Y 2
#endif

#ifndef NV3001B_TEXT_SIZE2_SCALE_X
  #define NV3001B_TEXT_SIZE2_SCALE_X 2
#endif

#ifndef NV3001B_TEXT_SIZE2_SCALE_Y
  #define NV3001B_TEXT_SIZE2_SCALE_Y 3
#endif

static uint16_t mapColor(DisplayDriver::Color c) {
  switch (c) {
    case DisplayDriver::DARK: return 0x0000;
    case DisplayDriver::LIGHT: return 0xffff;
    case DisplayDriver::RED: return 0xf800;
    case DisplayDriver::GREEN: return 0x07e0;
    case DisplayDriver::BLUE: return 0x001f;
    case DisplayDriver::YELLOW: return 0xffe0;
    case DisplayDriver::ORANGE: return 0xfd20;
    default: return 0xffff;
  }
}

static int scaleX(int x) {
  return (int)(x * DISPLAY_SCALE_X);
}

static int scaleY(int y) {
  return (int)(y * DISPLAY_SCALE_Y);
}

static int scaleWidth(int x, int w) {
  if (w <= 0) return 0;
  int scaled = scaleX(x + w) - scaleX(x);
  return scaled > 0 ? scaled : 1;
}

static int scaleHeight(int y, int h) {
  if (h <= 0) return 0;
  int scaled = scaleY(y + h) - scaleY(y);
  return scaled > 0 ? scaled : 1;
}

static uint8_t nv3001bMADCTL(uint8_t rotation) {
  uint8_t madctl;
  switch (rotation & 3) {
    case 0:
      madctl = NV3001B_MADCTL_MY | NV3001B_MADCTL_MV | NV3001B_MADCTL_RGB;
      break;
    case 1:
      madctl = NV3001B_MADCTL_MY | NV3001B_MADCTL_MX | NV3001B_MADCTL_RGB;
      break;
    case 2:
      madctl = NV3001B_MADCTL_RGB;
      break;
    default:
      madctl = NV3001B_MADCTL_MX | NV3001B_MADCTL_MV | NV3001B_MADCTL_RGB;
      break;
  }
  return madctl;
}

static const uint8_t font5x7[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5f, 0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x14,
  0x7f, 0x14, 0x7f, 0x14, 0x24, 0x2a, 0x7f, 0x2a, 0x12, 0x23, 0x13, 0x08, 0x64, 0x62, 0x36, 0x49,
  0x55, 0x22, 0x50, 0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x1c, 0x22, 0x41, 0x00, 0x00, 0x41, 0x22,
  0x1c, 0x00, 0x14, 0x08, 0x3e, 0x08, 0x14, 0x08, 0x08, 0x3e, 0x08, 0x08, 0x00, 0x50, 0x30, 0x00,
  0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x60, 0x60, 0x00, 0x00, 0x20, 0x10, 0x08, 0x04, 0x02,
  0x3e, 0x51, 0x49, 0x45, 0x3e, 0x00, 0x42, 0x7f, 0x40, 0x00, 0x42, 0x61, 0x51, 0x49, 0x46, 0x21,
  0x41, 0x45, 0x4b, 0x31, 0x18, 0x14, 0x12, 0x7f, 0x10, 0x27, 0x45, 0x45, 0x45, 0x39, 0x3c, 0x4a,
  0x49, 0x49, 0x30, 0x01, 0x71, 0x09, 0x05, 0x03, 0x36, 0x49, 0x49, 0x49, 0x36, 0x06, 0x49, 0x49,
  0x29, 0x1e, 0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x56, 0x36, 0x00, 0x00, 0x08, 0x14, 0x22, 0x41,
  0x00, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x41, 0x22, 0x14, 0x08, 0x02, 0x01, 0x51, 0x09, 0x06,
  0x32, 0x49, 0x79, 0x41, 0x3e, 0x7e, 0x11, 0x11, 0x11, 0x7e, 0x7f, 0x49, 0x49, 0x49, 0x36, 0x3e,
  0x41, 0x41, 0x41, 0x22, 0x7f, 0x41, 0x41, 0x22, 0x1c, 0x7f, 0x49, 0x49, 0x49, 0x41, 0x7f, 0x09,
  0x09, 0x09, 0x01, 0x3e, 0x41, 0x49, 0x49, 0x7a, 0x7f, 0x08, 0x08, 0x08, 0x7f, 0x00, 0x41, 0x7f,
  0x41, 0x00, 0x20, 0x40, 0x41, 0x3f, 0x01, 0x7f, 0x08, 0x14, 0x22, 0x41, 0x7f, 0x40, 0x40, 0x40,
  0x40, 0x7f, 0x02, 0x0c, 0x02, 0x7f, 0x7f, 0x04, 0x08, 0x10, 0x7f, 0x3e, 0x41, 0x41, 0x41, 0x3e,
  0x7f, 0x09, 0x09, 0x09, 0x06, 0x3e, 0x41, 0x51, 0x21, 0x5e, 0x7f, 0x09, 0x19, 0x29, 0x46, 0x46,
  0x49, 0x49, 0x49, 0x31, 0x01, 0x01, 0x7f, 0x01, 0x01, 0x3f, 0x40, 0x40, 0x40, 0x3f, 0x1f, 0x20,
  0x40, 0x20, 0x1f, 0x3f, 0x40, 0x38, 0x40, 0x3f, 0x63, 0x14, 0x08, 0x14, 0x63, 0x07, 0x08, 0x70,
  0x08, 0x07, 0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x7f, 0x41, 0x41, 0x00, 0x02, 0x04, 0x08, 0x10,
  0x20, 0x00, 0x41, 0x41, 0x7f, 0x00, 0x04, 0x02, 0x01, 0x02, 0x04, 0x40, 0x40, 0x40, 0x40, 0x40,
  0x00, 0x01, 0x02, 0x04, 0x00, 0x20, 0x54, 0x54, 0x54, 0x78, 0x7f, 0x48, 0x44, 0x44, 0x38, 0x38,
  0x44, 0x44, 0x44, 0x20, 0x38, 0x44, 0x44, 0x48, 0x7f, 0x38, 0x54, 0x54, 0x54, 0x18, 0x08, 0x7e,
  0x09, 0x01, 0x02, 0x0c, 0x52, 0x52, 0x52, 0x3e, 0x7f, 0x08, 0x04, 0x04, 0x78, 0x00, 0x44, 0x7d,
  0x40, 0x00, 0x20, 0x40, 0x44, 0x3d, 0x00, 0x7f, 0x10, 0x28, 0x44, 0x00, 0x00, 0x41, 0x7f, 0x40,
  0x00, 0x7c, 0x04, 0x18, 0x04, 0x78, 0x7c, 0x08, 0x04, 0x04, 0x78, 0x38, 0x44, 0x44, 0x44, 0x38,
  0x7c, 0x14, 0x14, 0x14, 0x08, 0x08, 0x14, 0x14, 0x18, 0x7c, 0x7c, 0x08, 0x04, 0x04, 0x08, 0x48,
  0x54, 0x54, 0x54, 0x20, 0x04, 0x3f, 0x44, 0x40, 0x20, 0x3c, 0x40, 0x40, 0x20, 0x7c, 0x1c, 0x20,
  0x40, 0x20, 0x1c, 0x3c, 0x40, 0x30, 0x40, 0x3c, 0x44, 0x28, 0x10, 0x28, 0x44, 0x0c, 0x50, 0x50,
  0x50, 0x3c, 0x44, 0x64, 0x54, 0x4c, 0x44, 0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x00, 0x7f, 0x00,
  0x00, 0x00, 0x41, 0x36, 0x08, 0x00, 0x08, 0x08, 0x2a, 0x1c, 0x08, 0x00, 0x06, 0x09, 0x09, 0x06
};

static int textPixelScaleX(uint8_t size) {
  return size <= 1 ? NV3001B_TEXT_SIZE1_SCALE_X : NV3001B_TEXT_SIZE2_SCALE_X;
}

static int textPixelScaleY(uint8_t size) {
  return size <= 1 ? NV3001B_TEXT_SIZE1_SCALE_Y : NV3001B_TEXT_SIZE2_SCALE_Y;
}

static void setupOptionalOutput(int pin, int level) {
  if (pin < 0) return;

  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
}

static void writeOptionalPin(int pin, int level) {
  if (pin < 0) return;

  digitalWrite(pin, level);
}

void NV3001BDisplay::writeCommand(uint8_t cmd) {
  spi.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TFT_DC, LOW);
  digitalWrite(PIN_TFT_CS, LOW);
  spi.transfer(cmd);
  digitalWrite(PIN_TFT_CS, HIGH);
  spi.endTransaction();
}

void NV3001BDisplay::writeBytes(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;

  spi.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TFT_DC, HIGH);
  digitalWrite(PIN_TFT_CS, LOW);
  for (size_t i = 0; i < len; i++) {
    spi.transfer(data[i]);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  spi.endTransaction();
}

void NV3001BDisplay::writeCommandData(uint8_t cmd, const uint8_t* data, size_t len) {
  writeCommand(cmd);
  writeBytes(data, len);
}

void NV3001BDisplay::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  uint16_t x2 = x + w - 1;
  uint16_t y2 = y + h - 1;
  uint8_t data[4];

  data[0] = x >> 8;
  data[1] = x & 0xff;
  data[2] = x2 >> 8;
  data[3] = x2 & 0xff;
  writeCommandData(NV3001B_CASET, data, sizeof(data));

  data[0] = y >> 8;
  data[1] = y & 0xff;
  data[2] = y2 >> 8;
  data[3] = y2 & 0xff;
  writeCommandData(NV3001B_RASET, data, sizeof(data));

  writeCommand(NV3001B_RAMWR);
}

void NV3001BDisplay::writeColor(uint16_t rgb, uint32_t count) {
  uint8_t hi = rgb >> 8;
  uint8_t lo = rgb & 0xff;

  spi.beginTransaction(SPISettings(SPI_FREQUENCY, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TFT_DC, HIGH);
  digitalWrite(PIN_TFT_CS, LOW);
  while (count--) {
    spi.transfer(hi);
    spi.transfer(lo);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  spi.endTransaction();
}

void NV3001BDisplay::initPanel() {
#define CMD0(C) do { writeCommand(C); } while (0)
#define CMD1(C, A) do { const uint8_t d[] = { A }; writeCommandData(C, d, sizeof(d)); } while (0)
#define CMD2(C, A, B) do { const uint8_t d[] = { A, B }; writeCommandData(C, d, sizeof(d)); } while (0)

  CMD0(NV3001B_SWRESET);
  delay(120);
  CMD1(0xFF, 0xA5);
  CMD1(0x41, 0x00);
  CMD1(0x50, 0x02);
  CMD1(0x52, 0x6E);
  CMD1(0x57, 0x02);
  CMD1(0x46, 0x11);
  CMD2(0x47, 0x00, 0x01);
  CMD2(0x8F, 0x22, 0x03);
  CMD1(0x9A, 0x78);
  CMD1(0x9B, 0x78);
  CMD1(0x9C, 0xA0);
  CMD1(0x9D, 0x17);
  CMD1(0x9E, 0xC1);
  CMD1(0x83, 0x5A);
  CMD1(0x84, 0xB6);
  CMD1(0xFF, 0xA5);
  CMD1(0x85, 0x5F);
  CMD1(0x6E, 0x0F);
  CMD1(0x7E, 0x0F);
  CMD1(0x60, 0x00);
  CMD1(0x70, 0x00);
  CMD1(0x6D, 0x33);
  CMD1(0x7D, 0x37);
  CMD1(0x61, 0x09);
  CMD1(0x71, 0x0A);
  CMD1(0x6C, 0x2A);
  CMD1(0x7C, 0x36);
  CMD1(0x62, 0x11);
  CMD1(0x72, 0x10);
  CMD1(0x68, 0x4E);
  CMD1(0x78, 0x4E);
  CMD1(0x66, 0x36);
  CMD1(0x76, 0x3C);
  CMD1(0x1A, 0x1C);
  CMD1(0x7B, 0x14);
  CMD1(0x63, 0x0D);
  CMD1(0x73, 0x0A);
  CMD1(0x6A, 0x16);
  CMD1(0x7A, 0x12);
  CMD1(0x64, 0x0B);
  CMD1(0x74, 0x0A);
  CMD1(0x69, 0x08);
  CMD1(0x79, 0x0A);
  CMD1(0x65, 0x06);
  CMD1(0x75, 0x07);
  CMD1(0x67, 0x23);
  CMD1(0x77, 0x44);
  CMD1(0xE0, 0x00);
  CMD1(0xE9, 0x30);
  CMD1(0xEB, 0xB7);
  CMD1(0xEC, 0x00);
  CMD1(0xED, 0x11);
  CMD1(0xF0, 0xB7);
  CMD1(0x53, 0x04);
  CMD1(0x54, 0x04);
  CMD1(0x55, 0x40);
  CMD1(0x56, 0x40);
  CMD2(0xA0, 0x60, 0x01);
  CMD1(0xA1, 0x84);
  CMD1(0xA2, 0x85);
  CMD2(0xAB, 0x00, 0x02);
  CMD2(0xAC, 0x00, 0x06);
  CMD2(0xAD, 0x00, 0x03);
  CMD2(0xAE, 0x00, 0x07);
  CMD1(0xC7, 0x01);
  CMD1(0xB9, 0x82);
  CMD1(0xBA, 0x83);
  CMD1(0xBB, 0x00);
  CMD1(0xBC, 0x81);
  CMD1(0xBD, 0x02);
  CMD1(0xBE, 0x01);
  CMD1(0xBF, 0x04);
  CMD1(0xC0, 0x03);
  CMD1(0xC8, 0x55);
  CMD1(0xC9, 0xC9);
  CMD1(0xCA, 0xC8);
  CMD1(0xCB, 0xCB);
  CMD1(0xCC, 0xCA);
  CMD1(0xCD, 0x55);
  CMD1(0xCE, 0xCE);
  CMD1(0xCF, 0xCD);
  CMD1(0xD0, 0xD0);
  CMD1(0xD1, 0xCF);
  CMD1(0xF2, 0x46);
  CMD1(0xA8, 0x04);
  CMD1(0xA9, 0xB0);
  CMD1(0xAA, 0xA3);
  CMD1(0xB6, 0x00);
  CMD1(0xB7, 0xB0);
  CMD1(0xB8, 0xA3);
  CMD1(0xC4, 0x03);
  CMD1(0xC5, 0xB0);
  CMD1(0xC6, 0xA3);
  CMD1(0x80, 0x10);
  CMD1(0xFF, 0x00);
  CMD1(0x35, 0x00);
  CMD0(NV3001B_SLPOUT);
  delay(120);
  CMD1(NV3001B_COLMOD, 0x05);
  CMD1(NV3001B_MADCTL, nv3001bMADCTL(DISPLAY_ROTATION));
  CMD0(NV3001B_DISPON);
  delay(10);

#undef CMD0
#undef CMD1
#undef CMD2
}

void NV3001BDisplay::fillPhysicalRect(int x, int y, int w, int h) {
  if (!is_on || w <= 0 || h <= 0) return;

  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > NV3001B_SCREEN_WIDTH) w = NV3001B_SCREEN_WIDTH - x;
  if (y + h > NV3001B_SCREEN_HEIGHT) h = NV3001B_SCREEN_HEIGHT - y;
  if (w <= 0 || h <= 0) return;

  setAddrWindow(x, y, w, h);
  writeColor(color, (uint32_t)w * h);
}

void NV3001BDisplay::drawChar(int x, int y, char ch) {
  if (ch < 32 || ch > 127) ch = '?';

  uint16_t index = (uint16_t)(ch - 32) * 5;
  int scale_x = textPixelScaleX(text_size);
  int scale_y = textPixelScaleY(text_size);
  for (int col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(font5x7 + index + col);
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        fillPhysicalRect(x + col * scale_x, y + row * scale_y, scale_x, scale_y);
      }
    }
  }
}

bool NV3001BDisplay::begin() {
  if (is_on) return true;

  if (periph_power) periph_power->claim();

  setupOptionalOutput(PIN_TFT_EN, PIN_TFT_EN_ACTIVE);
  setupOptionalOutput(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TFT_DC, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TFT_DC, HIGH);
  delay(20);

  spi.begin(PIN_TFT_SCL, PIN_TFT_MISO, PIN_TFT_SDA, PIN_TFT_CS);
  if (PIN_TFT_RST >= 0) {
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(10);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(20);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(120);
  }

  initPanel();
  is_on = true;
  color = 0x0000;
  fillPhysicalRect(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
  color = 0xffff;
  text_size = 1;
  cursor_x = 0;
  cursor_y = 0;
  writeOptionalPin(PIN_TFT_BL, PIN_TFT_BL_ACTIVE);
  return true;
}

void NV3001BDisplay::turnOn() {
  begin();
}

void NV3001BDisplay::turnOff() {
  if (!is_on) return;

  writeOptionalPin(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
  writeOptionalPin(PIN_TFT_EN, !PIN_TFT_EN_ACTIVE);
  is_on = false;
  if (periph_power) periph_power->release();
}

void NV3001BDisplay::clear() {
  uint16_t saved = color;
  color = 0x0000;
  fillPhysicalRect(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
  color = saved;
}

void NV3001BDisplay::startFrame(Color bkg) {
  color = mapColor(bkg);
  fillPhysicalRect(0, 0, NV3001B_SCREEN_WIDTH, NV3001B_SCREEN_HEIGHT);
  color = 0xffff;
  text_size = 1;
  cursor_x = 0;
  cursor_y = 0;
}

void NV3001BDisplay::setTextSize(int sz) {
  text_size = sz < 1 ? 1 : sz;
}

void NV3001BDisplay::setColor(Color c) {
  color = mapColor(c);
}

void NV3001BDisplay::setCursor(int x, int y) {
  cursor_x = scaleX(x);
  cursor_y = scaleY(y);
}

void NV3001BDisplay::print(const char* str) {
  if (!str || !is_on) return;

  int scale_x = textPixelScaleX(text_size);
  int scale_y = textPixelScaleY(text_size);
  while (*str) {
    if (*str == '\n') {
      cursor_x = 0;
      cursor_y += 8 * scale_y;
    } else if (*str == '\r') {
      cursor_x = 0;
    } else {
      drawChar(cursor_x, cursor_y, *str);
      cursor_x += 6 * scale_x;
    }
    str++;
  }
}

void NV3001BDisplay::fillRect(int x, int y, int w, int h) {
  fillPhysicalRect(scaleX(x), scaleY(y), scaleWidth(x, w), scaleHeight(y, h));
}

void NV3001BDisplay::drawRect(int x, int y, int w, int h) {
  int x1 = scaleX(x);
  int y1 = scaleY(y);
  int sw = scaleWidth(x, w);
  int sh = scaleHeight(y, h);

  fillPhysicalRect(x1, y1, sw, 1);
  fillPhysicalRect(x1, y1 + sh - 1, sw, 1);
  fillPhysicalRect(x1, y1, 1, sh);
  fillPhysicalRect(x1 + sw - 1, y1, 1, sh);
}

void NV3001BDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  if (!bits || !is_on) return;

  int byte_width = (w + 7) / 8;
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      uint8_t byte = pgm_read_byte(bits + j * byte_width + i / 8);
      if (byte & (0x80 >> (i & 7))) {
        fillPhysicalRect(scaleX(x + i), scaleY(y + j), scaleWidth(x + i, 1), scaleHeight(y + j, 1));
      }
    }
  }
}

uint16_t NV3001BDisplay::getTextWidth(const char* str) {
  if (!str) return 0;

  uint16_t len = 0;
  while (str[len] && str[len] != '\n' && str[len] != '\r') len++;
  return (uint16_t)((len * 6 * textPixelScaleX(text_size)) / DISPLAY_SCALE_X);
}

void NV3001BDisplay::endFrame() {
}
