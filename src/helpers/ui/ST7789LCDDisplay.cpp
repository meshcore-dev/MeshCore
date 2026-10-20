#include "ST7789LCDDisplay.h"

#ifdef LILYGO_TDECK
#include <esp_heap_caps.h>
#include <new>
#include <string.h>

// Adafruit_GFX supplies text and bitmap rasterization without allocating its
// own framebuffer. The 153,600-byte RGB565 frame must live in external RAM.
class TDeckCanvas : public Adafruit_GFX {
  ST7789LCDDisplay& _owner;
  uint16_t* _pixels;
  uint16_t _pixel_work = 0;

  void cooperate(uint16_t pixels) {
    _pixel_work += pixels;
    if (_pixel_work >= 128) {
      _pixel_work = 0;
      _owner.serviceRender();
    }
  }

public:
  TDeckCanvas(ST7789LCDDisplay& owner, int w, int h)
      : Adafruit_GFX(w, h), _owner(owner),
        _pixels(static_cast<uint16_t*>(heap_caps_calloc(
            size_t(w) * h, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {
    cp437(true);
  }
  ~TDeckCanvas() { heap_caps_free(_pixels); }
  uint16_t* pixels() const { return _pixels; }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (_pixels && x >= 0 && y >= 0 && x < WIDTH && y < HEIGHT)
      _pixels[size_t(y) * WIDTH + x] = color;
    cooperate(1);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    if (!_pixels || y < 0 || y >= HEIGHT || w <= 0) return;
    const int left = x < 0 ? 0 : x;
    const int right = int(x) + w > WIDTH ? WIDTH : int(x) + w;
    if (left >= right) return;
    uint16_t* row = _pixels + size_t(y) * WIDTH;
    for (int xx = left; xx < right; ++xx) row[xx] = color;
    cooperate(right - left);
  }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    if (!_pixels || x < 0 || x >= WIDTH || h <= 0) return;
    const int top = y < 0 ? 0 : y;
    const int bottom = int(y) + h > HEIGHT ? HEIGHT : int(y) + h;
    for (int yy = top; yy < bottom; ++yy) {
      _pixels[size_t(yy) * WIDTH + x] = color;
      cooperate(1);
    }
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (w <= 0 || h <= 0) return;
    const int top = y < 0 ? 0 : y;
    const int bottom = int(y) + h > HEIGHT ? HEIGHT : int(y) + h;
    for (int yy = top; yy < bottom; ++yy) drawFastHLine(x, yy, w, color);
  }
  void fillScreen(uint16_t color) override { fillRect(0, 0, WIDTH, HEIGHT, color); }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    drawFastHLine(x, y, w, color);
  }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    drawFastVLine(x, y, h, color);
  }
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    fillRect(x, y, w, h, color);
  }
};

ST7789LCDDisplay::~ST7789LCDDisplay() { delete _canvas; }

bool ST7789LCDDisplay::prepareCanvas() {
  if (_canvas_attempted) return _canvas != nullptr;
  _canvas_attempted = true;
  _canvas = new (std::nothrow) TDeckCanvas(*this, display.width(), display.height());
  if (!_canvas || !_canvas->pixels()) {
    delete _canvas;
    _canvas = nullptr;
    Serial.println("WARN: T-Deck display PSRAM allocation failed; using direct redraw");
    return false;
  }
  return true;
}

bool ST7789LCDDisplay::isBufferedRendering() const {
  return _buffered_rendering && _canvas != nullptr;
}

void ST7789LCDDisplay::setBufferedRendering(bool enabled) {
  _frame_active = false;
  _buffered_rendering = enabled;
  if (enabled && _panel_ready) prepareCanvas();
}

void ST7789LCDDisplay::serviceRender(bool force) {
  if (!_buffered_rendering || !_render_yield || _render_callback_active) return;
  if (!force && uint32_t(micros() - _last_render_yield_us) < 250) return;
  _render_callback_active = true;
  _render_yield();
  _render_callback_active = false;
  _last_render_yield_us = micros();
}

void ST7789LCDDisplay::flushCanvas(int x, int y, int w, int h) {
  if (!isBufferedRendering() || !_isOn || w <= 0 || h <= 0) return;
  const int right = int32_t(x) + w > display.width() ? display.width() : int32_t(x) + w;
  const int bottom = int32_t(y) + h > display.height() ? display.height() : int32_t(y) + h;
  x = x < 0 ? 0 : x;
  y = y < 0 ? 0 : y;
  w = right - x;
  if (w <= 0 || y >= bottom) return;

  // At most 960 pixels (0.384 ms at 40 MHz), plus command/driver overhead,
  // per transaction. Copy out of PSRAM so SPI never depends on DMA from it.
  constexpr size_t StripPixels = 960;
  uint16_t strip[StripPixels];
  const int rows_per_strip = StripPixels / w;
  serviceRender(true);
  while (y < bottom) {
    const int rows = bottom - y < rows_per_strip ? bottom - y : rows_per_strip;
    for (int row = 0; row < rows; ++row) {
      memcpy(strip + size_t(row) * w,
             _canvas->pixels() + size_t(y + row) * display.width() + x,
             size_t(w) * sizeof(uint16_t));
    }
    display.startWrite();
    display.setAddrWindow(x, y, w, rows);
    display.writePixels(strip, size_t(w) * rows, true);
    display.endWrite();
    // CS is high and the transfer is complete before the radio can use the
    // shared physical clock/data pins. No recursive application-loop call.
    serviceRender(true);
    y += rows;
  }
}

void ST7789LCDDisplay::finishCanvasPrimitive(int x, int y, int w, int h) {
  serviceRender();
  // Dashboard rows can be updated without startFrame/endFrame. Preserve their
  // immediate behavior while sending only their changed rectangle in strips.
  if (!_frame_active) flushCanvas(x, y, w, h);
}
#endif


#ifndef PIN_TFT_MISO
  #define PIN_TFT_MISO -1
#endif

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

// Color scheme
ColorVal UIColor::window_bkg = ST77XX_WHITE;
ColorVal UIColor::title_bkg = ST77XX_BLUE;
ColorVal UIColor::title_txt = ST77XX_WHITE;
ColorVal UIColor::primary_txt = ST77XX_BLACK;
ColorVal UIColor::secondary_txt = (18 << 11) | (36 << 5) | 18;  // mid-gray
ColorVal UIColor::warning_txt = ST77XX_ORANGE;
ColorVal UIColor::popup_bkg = ST77XX_CYAN;
ColorVal UIColor::popup_txt = ST77XX_BLACK;
ColorVal UIColor::corp_blue = 0x001A;

bool ST7789LCDDisplay::begin() {
  if (!_isOn) {
#ifdef LILYGO_TDECK
    if (_panel_ready) {
      if (_peripher_power) _peripher_power->claim();
      if (PIN_TFT_LEDA_CTL != -1) digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
      _isOn = true;
      return true;
    }
#endif
    if (_peripher_power) _peripher_power->claim();

    if (PIN_TFT_LEDA_CTL != -1) {
      pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
      digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
    }

    // Im not sure if this is just a t-deck problem or not, if your display is slow try this.
    #if defined(LILYGO_TDECK) || defined(HELTEC_LORA_V4_TFT) || defined(HELTEC_V4_R8_TFT)
      displaySPI.begin(PIN_TFT_SCL, PIN_TFT_MISO, PIN_TFT_SDA, PIN_TFT_CS);
    #endif

    display.init(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    display.setRotation(DISPLAY_ROTATION);

    display.setSPISpeed(40e6);

    display.fillScreen(ST77XX_BLACK);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2 * DISPLAY_SCALE_X); 
    display.cp437(true); // Use full 256 char 'Code Page 437' font
  
    _isOn = true;
#ifdef LILYGO_TDECK
    _panel_ready = true;
    if (_buffered_rendering) prepareCanvas();
#endif
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
#ifdef LILYGO_TDECK
  if (!_isOn) return;
  if (isBufferedRendering()) {
    _canvas->fillScreen(ST77XX_BLACK);
    finishCanvasPrimitive(0, 0, display.width(), display.height());
    return;
  }
#endif
  display.fillScreen(ST77XX_BLACK);
}

void ST7789LCDDisplay::startFrame(ColorVal bkg) {
#ifdef LILYGO_TDECK
  if (!_isOn) return;
  if (isBufferedRendering()) {
    _frame_active = true;
    serviceRender(true);
    _canvas->fillScreen(bkg);
    _canvas->setTextColor(_color = UIColor::primary_txt);
    _canvas->setTextSize(1 * DISPLAY_SCALE_X);
    _canvas->cp437(true);
    return;
  }
#endif
  display.fillScreen(bkg);
  display.setTextColor(_color = UIColor::primary_txt);
  display.setTextSize(1 * DISPLAY_SCALE_X); // This one affects size of Please wait... message
  display.cp437(true); // Use full 256 char 'Code Page 437' font
}

void ST7789LCDDisplay::setTextSize(int sz) {
#ifdef LILYGO_TDECK
  if (!_isOn) return;
  if (isBufferedRendering()) {
    _canvas->setTextSize(sz * DISPLAY_SCALE_X);
    return;
  }
#endif
  display.setTextSize(sz * DISPLAY_SCALE_X);
}

void ST7789LCDDisplay::setColor(ColorVal c) {
#ifdef LILYGO_TDECK
  if (!_isOn) return;
  if (isBufferedRendering()) {
    _canvas->setTextColor(_color = c);
    return;
  }
#endif
  display.setTextColor(_color = c);
}

void ST7789LCDDisplay::setCursor(int x, int y) {
#ifdef LILYGO_TDECK
  if (!_isOn) return;
  if (isBufferedRendering()) {
    _canvas->setCursor(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y);
    return;
  }
#endif
  display.setCursor(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y);
}

void ST7789LCDDisplay::print(const char* str) {
#ifdef LILYGO_TDECK
  if (!_isOn) return;
  if (isBufferedRendering()) {
    int16_t x, y;
    uint16_t w, h;
    _canvas->getTextBounds(str, _canvas->getCursorX(), _canvas->getCursorY(), &x, &y, &w, &h);
    _canvas->print(str);
    finishCanvasPrimitive(x, y, w, h);
    return;
  }
#endif
  display.print(str);
}

void ST7789LCDDisplay::fillRect(int x, int y, int w, int h) {
#ifdef LILYGO_TDECK
  if (!_isOn) return;
  if (isBufferedRendering()) {
    x *= DISPLAY_SCALE_X; y *= DISPLAY_SCALE_Y;
    w *= DISPLAY_SCALE_X; h *= DISPLAY_SCALE_Y;
    _canvas->fillRect(x, y, w, h, _color);
    finishCanvasPrimitive(x, y, w, h);
    return;
  }
#endif
  display.fillRect(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y, w * DISPLAY_SCALE_X, h * DISPLAY_SCALE_Y, _color);
}

void ST7789LCDDisplay::drawRect(int x, int y, int w, int h) {
#ifdef LILYGO_TDECK
  if (!_isOn) return;
  if (isBufferedRendering()) {
    x *= DISPLAY_SCALE_X; y *= DISPLAY_SCALE_Y;
    w *= DISPLAY_SCALE_X; h *= DISPLAY_SCALE_Y;
    _canvas->drawRect(x, y, w, h, _color);
    finishCanvasPrimitive(x, y, w, h);
    return;
  }
#endif
  display.drawRect(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y, w * DISPLAY_SCALE_X, h * DISPLAY_SCALE_Y, _color);
}

void ST7789LCDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
#ifdef LILYGO_TDECK
  if (!_isOn) return;
  Adafruit_GFX& target = isBufferedRendering() ? static_cast<Adafruit_GFX&>(*_canvas) : display;
#else
  Adafruit_GFX& target = display;
#endif
  uint8_t byteWidth = (w + 7) / 8;

  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      uint8_t byte = bits[j * byteWidth + i / 8];
      bool pixelOn = byte & (0x80 >> (i & 7));

      if (pixelOn) {
        for (int dy = 0; dy < DISPLAY_SCALE_X; dy++) {
          for (int dx = 0; dx < DISPLAY_SCALE_X; dx++) {
            target.drawPixel(x * DISPLAY_SCALE_X + i * DISPLAY_SCALE_X + dx, y * DISPLAY_SCALE_Y + j * DISPLAY_SCALE_X + dy, _color);
          }
        }
      }
    }
  }
#ifdef LILYGO_TDECK
  if (isBufferedRendering()) finishCanvasPrimitive(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y,
      w * DISPLAY_SCALE_X + 1, h * DISPLAY_SCALE_X + 1);
#endif
}

uint16_t ST7789LCDDisplay::getTextWidth(const char* str) {
#ifdef LILYGO_TDECK
  if (!_isOn) return 0;
  if (isBufferedRendering()) {
    int16_t x, y;
    uint16_t w, h;
    _canvas->getTextBounds(str, 0, 0, &x, &y, &w, &h);
    return w / DISPLAY_SCALE_X;
  }
#endif
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);

  return w / DISPLAY_SCALE_X;
}

void ST7789LCDDisplay::endFrame() {
#ifdef LILYGO_TDECK
  _frame_active = false;
  if (isBufferedRendering()) flushCanvas(0, 0, display.width(), display.height());
#endif
  // display.display();
}
