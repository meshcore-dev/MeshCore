#include "SSD1306SPIDisplay.h"

// Check if SPI is ready (set by radio_init in target.cpp)
#if defined(P_LORA_SCLK)
  extern bool spi_initialized;
#else
  static bool spi_initialized = true;  // Assume ready if no custom SPI
#endif

bool SSD1306SPIDisplay::begin() {
  // Defer actual initialization - SPI may not be ready yet
  // Real init happens in lazyInit() on first use (after radio_init)
  return true;
}

bool SSD1306SPIDisplay::lazyInit() {
  if (_initialized) return true;
  if (!spi_initialized) {
    Serial.println("SSD1306: SPI not initialized yet");
    return false;
  }

  Serial.println("SSD1306: Attempting display init...");
  #ifdef DISPLAY_ROTATION
  display.setRotation(DISPLAY_ROTATION);
  #endif
  // SPI is now initialized by radio_init()
  // Pass periphBegin=false to skip spi.begin() since radio already did it
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0, true, false)) {
    Serial.println("SSD1306: display.begin() FAILED");
    return false;
  }
  Serial.println("SSD1306: display.begin() OK");

  // Fix for 64x48 displays: Adafruit library lacks this case and defaults
  // to comPins=0x02 (sequential). Displays taller than 32px need 0x12
  // (alternative COM pin config) or the output is garbled.
  #if defined(DISPLAY_WIDTH) && defined(DISPLAY_HEIGHT)
  #if (DISPLAY_WIDTH == 64) && (DISPLAY_HEIGHT == 48)
  display.ssd1306_command(SSD1306_SETCOMPINS);
  display.ssd1306_command(0x12);
  #endif
  #endif

  // Clear any garbage in the display buffer
  display.clearDisplay();
  display.display();
  _initialized = true;
  return true;
}

void SSD1306SPIDisplay::turnOn() {
  if (!lazyInit()) return;
  display.ssd1306_command(SSD1306_DISPLAYON);
  _isOn = true;
}

void SSD1306SPIDisplay::turnOff() {
  if (!lazyInit()) return;
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  _isOn = false;
}

void SSD1306SPIDisplay::clear() {
  if (!lazyInit()) return;
  display.clearDisplay();
  display.display();
}

void SSD1306SPIDisplay::startFrame(Color bkg) {
  if (!lazyInit()) return;
  display.clearDisplay();  // TODO: apply 'bkg'
  _color = SSD1306_WHITE;
  display.setTextColor(_color);
  display.setFont(NULL);  // Default 6x8 font
  display.setTextSize(1);
  display.setTextWrap(false);
  display.cp437(true);
}

void SSD1306SPIDisplay::setTextSize(int sz) {
  display.setTextSize(sz);
}

void SSD1306SPIDisplay::setColor(Color c) {
  _color = (c != 0) ? SSD1306_WHITE : SSD1306_BLACK;
  display.setTextColor(_color);
}

void SSD1306SPIDisplay::setCursor(int x, int y) {
  display.setCursor(x, y);
}

void SSD1306SPIDisplay::print(const char* str) {
  display.print(str);
}

void SSD1306SPIDisplay::fillRect(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, _color);
}

void SSD1306SPIDisplay::drawRect(int x, int y, int w, int h) {
  display.drawRect(x, y, w, h, _color);
}

void SSD1306SPIDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display.drawBitmap(x, y, bits, w, h, SSD1306_WHITE);
}

uint16_t SSD1306SPIDisplay::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SSD1306SPIDisplay::endFrame() {
  if (!_initialized) return;
  display.display();
}
