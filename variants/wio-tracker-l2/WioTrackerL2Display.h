#pragma once

#include <helpers/ui/LGFXDisplay.h>
#include <Wire.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ---------------------------------------------------------------------------
// Wio Tracker L2 Pro display stack:
//   - NV3031B 320x240 panel on quad-SPI (SCLK 42, IO0-3 = 41/40/39/38, CS 46)
//   - GT911 capacitive touch on I2C 0x5D (SDA 47 / SCL 48)
//   - LP5814 4-channel LED driver on I2C 0x2C used as backlight
// Panel power/reset lines are on the TCA9535 expander and are already
// sequenced by WioTrackerL2Board::begin() before this driver initializes.
// ---------------------------------------------------------------------------

#ifndef L2_SPI_FREQUENCY
  #define L2_SPI_FREQUENCY 75000000
#endif

#define LP5814_I2C_ADDR 0x2C

// LP5814 used as backlight controller (all 4 channels in parallel)
class WioTrackerL2Backlight : public lgfx::v1::ILight {
  static constexpr uint8_t REG_DEVICE_CONFIG0 = 0x00;
  static constexpr uint8_t REG_MAX_CURRENT = 0x01;
  static constexpr uint8_t REG_ENABLE_CONTROL = 0x02;
  static constexpr uint8_t REG_DIM_MODE = 0x04;
  static constexpr uint8_t REG_ENGINE_MODE = 0x05;
  static constexpr uint8_t REG_UPDATE = 0x0F;
  static constexpr uint8_t REG_LED0_DC = 0x14;
  static constexpr uint8_t REG_LED0_PWM = 0x18;

  uint8_t _brightness = 153;  // 60%

  void writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(LP5814_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }

public:
  bool init(uint8_t brightness) override {
    Wire.beginTransmission(LP5814_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
      return false;  // LP5814 not found
    }

    writeReg(REG_DEVICE_CONFIG0, 0x01);  // chip enable
    writeReg(REG_MAX_CURRENT, 0x01);     // 51 mA max current
    writeReg(REG_ENABLE_CONTROL, 0x00);  // outputs off while configuring
    writeReg(REG_DIM_MODE, 0x4E);
    writeReg(REG_ENGINE_MODE, 0xF0);
    for (uint8_t i = 0; i < 4; i++) {
      writeReg(REG_LED0_DC + i, 200);
    }
    writeReg(REG_ENABLE_CONTROL, 0x0F);  // enable all 4 channels
    writeReg(REG_UPDATE, 0x55);          // latch (LP5814 requires 0x55)
    delay(5);

    setBrightness(brightness);
    return true;
  }

  void setBrightness(uint8_t brightness) override {
    for (uint8_t i = 0; i < 4; i++) {
      writeReg(REG_LED0_PWM + i, brightness);
    }
    _brightness = brightness;
  }

  uint8_t getBrightness() const { return _brightness; }

  virtual ~WioTrackerL2Backlight() = default;
};

class LGFX_WioTrackerL2 : public lgfx::LGFX_Device {
  lgfx::Panel_NV3031B _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Touch_GT911 _touch_instance;
  WioTrackerL2Backlight _light_instance;

public:
  bool init_impl(bool use_reset, bool use_clear) override {
    // bring up backlight controller while the I2C bus is still clean
    _light_instance.init(_light_instance.getBrightness());

    bool result = LGFX_Device::init_impl(use_reset, use_clear);

    // GT911 probe can leave the ESP32 I2C peripheral with a stuck BUSY flag;
    // cycling Wire resets it so later LP5814/sensor traffic doesn't time out
    Wire.end();
    Wire.begin(47, 48);

    return result;
  }

  LGFX_WioTrackerL2(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI3_HOST;
      cfg.spi_mode = 3;
      cfg.freq_write = L2_SPI_FREQUENCY;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = 42;
      // quad SPI data pins
      cfg.pin_io0 = 41;
      cfg.pin_io1 = 40;
      cfg.pin_io2 = 39;
      cfg.pin_io3 = 38;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 46;
      cfg.pin_rst = -1;   // reset is on the IO expander, done in board init
      cfg.pin_busy = -1;
      cfg.panel_width = 240;   // native portrait orientation
      cfg.panel_height = 320;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 1;  // panel mounted landscape (320x240)
      cfg.invert = true;
      cfg.rgb_order = true;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    {
      auto cfg = _touch_instance.config();
      cfg.pin_cs = -1;
      cfg.x_min = 0;
      cfg.x_max = 239;
      cfg.y_min = 0;
      cfg.y_max = 319;
      cfg.pin_int = -1;   // INT is on the IO expander
      cfg.offset_rotation = 2;
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x5D;
      cfg.pin_sda = 47;
      cfg.pin_scl = 48;
      cfg.bus_shared = false;
      cfg.freq = 400000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    _panel_instance.setLight(&_light_instance);
    setPanel(&_panel_instance);
  }
};

class WioTrackerL2Display : public LGFXDisplay {
  LGFX_WioTrackerL2 disp;
public:
  WioTrackerL2Display() : LGFXDisplay(320, 240, disp) {}

  // direct access to the LGFX device (LVGL flush/touch glue)
  lgfx::LGFX_Device* lgfxDevice() { return &disp; }

  // reliable touch read in logical (UI_ZOOM-scaled) coords; returns false when
  // not touched (LGFXDisplay::getTouch reads an uninitialized point on release)
  bool readTouch(int& x, int& y) {
    lgfx::touch_point_t tp;
    if (disp.getTouch(&tp, 1) == 0) return false;
    x = tp.x / UI_ZOOM;
    y = tp.y / UI_ZOOM;
    return true;
  }

  // shadows LGFXDisplay::begin() (non-virtual, called on the concrete type in
  // main.cpp) - identical except rotation: our panel config already carries
  // offset_rotation=1 for the landscape mounting, so no extra rotation here
  bool begin() {
    // dark theme: deep navy ground, MeshCore blue accents (RGB565)
    UIColor::window_bkg    = 0x0885;   // dark navy
    UIColor::title_bkg     = 0x1299;   // meshcore blue
    UIColor::title_txt     = 0xFFFF;
    UIColor::primary_txt   = 0xE73C;   // near-white
    UIColor::secondary_txt = 0x8D59;   // muted blue-gray
    UIColor::warning_txt   = 0xFD20;   // orange
    UIColor::popup_bkg     = 0x1299;
    UIColor::popup_txt     = 0xFFFF;
    UIColor::corp_blue     = 0x5DBF;   // sky blue (own messages, icons)

    turnOn();
    display->init();
    display->setRotation(0);
    display->setColorDepth(8);
    // The zoomed sprite blit stops a pixel short of the panel edge, so those
    // pixels keep whatever the panel powered up with (they showed as coloured
    // lines down the right side and along the bottom). Clear the panel once.
    display->fillScreen(UIColor::window_bkg);
    display->setBrightness(153);
    display->setTextColor(TFT_WHITE);

    buffer.setColorDepth(8);
    buffer.setPsram(true);
    buffer.createSprite(width(), height());

    return true;
  }
};
