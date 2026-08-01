#pragma once

#include <GxEPD2_EPD.h>

// Good Display GDEM0154Z91: 1.54", 152x152, black/white/red, SSD1680.
// The controller stores black and red in separate 1-bit RAM planes and only
// supports the panel's OTP-driven full refresh waveform.
class GxEPD2_154_Z91c : public GxEPD2_EPD {
public:
  static const uint16_t WIDTH = 152;
  static const uint16_t WIDTH_VISIBLE = 152;
  static const uint16_t HEIGHT = 152;
  static const GxEPD2::Panel panel = GxEPD2::GDEY0266Z90;
  static const bool hasColor = true;
  static const bool hasPartialUpdate = false;
  static const bool hasFastPartialUpdate = false;
  static const uint16_t power_on_time = 100;
  static const uint16_t power_off_time = 150;
  static const uint16_t full_refresh_time = 15000;
  static const uint16_t partial_refresh_time = 15000;

  GxEPD2_154_Z91c(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

  void clearScreen(uint8_t value = 0xFF);
  void clearScreen(uint8_t black_value, uint8_t color_value);
  void writeScreenBuffer(uint8_t value = 0xFF);
  void writeScreenBuffer(uint8_t black_value, uint8_t color_value);
  void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                  bool invert = false, bool mirror_y = false, bool pgm = false);
  void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                      int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y,
                      int16_t w, int16_t h, bool invert = false,
                      bool mirror_y = false, bool pgm = false);
  void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y,
                  int16_t w, int16_t h, bool invert = false,
                  bool mirror_y = false, bool pgm = false);
  void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part,
                      int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                      int16_t x, int16_t y, int16_t w, int16_t h,
                      bool invert = false, bool mirror_y = false, bool pgm = false);
  void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y,
                   int16_t w, int16_t h, bool invert = false,
                   bool mirror_y = false, bool pgm = false);
  void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                 bool invert = false, bool mirror_y = false, bool pgm = false);
  void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part,
                     int16_t w_bitmap, int16_t h_bitmap, int16_t x, int16_t y,
                     int16_t w, int16_t h, bool invert = false,
                     bool mirror_y = false, bool pgm = false);
  void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y,
                 int16_t w, int16_t h, bool invert = false,
                 bool mirror_y = false, bool pgm = false);
  void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part,
                     int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                     int16_t x, int16_t y, int16_t w, int16_t h,
                     bool invert = false, bool mirror_y = false, bool pgm = false);
  void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y,
                  int16_t w, int16_t h, bool invert = false,
                  bool mirror_y = false, bool pgm = false);
  void refresh(bool partial_update_mode = false);
  void refresh(int16_t x, int16_t y, int16_t w, int16_t h);
  void powerOff();
  void hibernate();

private:
  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void initDisplay();
  void update();
};
