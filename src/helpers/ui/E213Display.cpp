#include "E213Display.h"

#include "../../MeshCore.h"

BaseDisplay* E213Display::detectEInk()
{
    // Test 1: Logic of BUSY pin

    // Determines controller IC manufacturer
    // Fitipower: busy when LOW
    // Solomon Systech: busy when HIGH

    // Force display BUSY by holding reset pin active
    pinMode(DISP_RST, OUTPUT);
    digitalWrite(DISP_RST, LOW);

    delay(10);

    // Read whether pin is HIGH or LOW while busy
    pinMode(DISP_BUSY, INPUT);
    bool busyLogic = digitalRead(DISP_BUSY);

    // Test complete. Release pin
    pinMode(DISP_RST, INPUT);

    if (busyLogic == LOW) {
#ifdef VISION_MASTER_E213
      return new EInkDisplay_VisionMasterE213 ;
#else
      return new EInkDisplay_WirelessPaperV1_1 ;
#endif
    } else {// busy HIGH
#ifdef VISION_MASTER_E213
      return new EInkDisplay_VisionMasterE213V1_1 ;
#else
      return new EInkDisplay_WirelessPaperV1_1_1 ;
#endif
    }
}

bool E213Display::begin() {
  if (_init) return true;

  powerOn();
  if(display==NULL) {
    display = detectEInk();
  }
  display->begin();
  // Set to landscape mode rotated 180 degrees
  display->setRotation(3);

  _init = true;
  _isOn = true;

  clear();
  display->fastmodeOn(); // Enable fast mode for quicker (partial) updates

  return true;
}

void E213Display::powerOn() {
  if (_periph_power) {
    _periph_power->claim();
  } else {
#ifdef PIN_VEXT_EN
    pinMode(PIN_VEXT_EN, OUTPUT);
#ifdef PIN_VEXT_EN_ACTIVE
    digitalWrite(PIN_VEXT_EN, PIN_VEXT_EN_ACTIVE);
#else
    digitalWrite(PIN_VEXT_EN, LOW); // Active low
#endif
#endif
  }
  delay(50);                      // Allow power to stabilize
}

void E213Display::powerOff() {
  if (_periph_power) {
    _periph_power->release();
  } else {
#ifdef PIN_VEXT_EN
#ifdef PIN_VEXT_EN_ACTIVE
    digitalWrite(PIN_VEXT_EN, !PIN_VEXT_EN_ACTIVE);
#else
    digitalWrite(PIN_VEXT_EN, HIGH); // Turn off power
#endif
#endif
  }
}

void E213Display::turnOn() {
  if (!_init) begin();
  else if (!_isOn) {
    powerOn();
    display->fastmodeOn();  // Reinitialize display controller after power was cut
  }
  _isOn = true;
}

void E213Display::turnOff() {
  if (_isOn) {
    powerOff();
    _isOn = false;
  }
}

void E213Display::clear() {
  display->clear();
}

void E213Display::startFrame(Color bkg) {
  display_crc.reset();

  // Fill screen with white first to ensure clean background
  display->fillRect(0, 0, width(), height(), WHITE);

  if (bkg == LIGHT) {
    // Fill with black if light background requested (inverted for e-ink)
    display->fillRect(0, 0, width(), height(), BLACK);
  }
}

void E213Display::setTextSize(int sz) {
  display_crc.update<int>(sz);
  // The library handles text size internally
    display->setTextSize(sz);
}

void E213Display::setColor(Color c) {
  display_crc.update<Color>(c);
  // implemented in individual display methods
}

void E213Display::setCursor(int x, int y) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
    display->setCursor(x, y);
}

void E213Display::print(const char *str) {
  display_crc.update<char>(str, strlen(str));
    display->print(str);
}

void E213Display::fillRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
    display->fillRect(x, y, w, h, BLACK);
}

void E213Display::drawRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
    display->drawRect(x, y, w, h, BLACK);
}

void E213Display::drawXbm(int x, int y, const uint8_t *bits, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display_crc.update<uint8_t>(bits, w * h / 8);

  // Width in bytes for bitmap processing
  uint16_t widthInBytes = (w + 7) / 8;

  // Process the bitmap row by row
  for (int by = 0; by < h; by++) {
    // Scan across the row bit by bit
    for (int bx = 0; bx < w; bx++) {
      // Get the current bit using MSB ordering (like GxEPDDisplay)
      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      bool bitSet = bits[byteOffset] & bitMask;

      // If the bit is set, draw the pixel
      if (bitSet) {
          display->drawPixel(x + bx, y + by, BLACK);
      }
    }
  }
}

uint16_t E213Display::getTextWidth(const char *str) {
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void E213Display::endFrame() {
  uint32_t crc = display_crc.finalize();
  if (crc != last_display_crc_value) {
    display->update();
    last_display_crc_value = crc;
  }
}
