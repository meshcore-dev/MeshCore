#pragma once

#include <helpers/ui/LGFXDisplay.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// Custom ST7701 panel subclass with SenseCap Indicator-specific init commands.
// The default Panel_ST7701 init sequence does not set MADCTL or SDIR, so the
// display outputs pixels in an orientation that doesn't match the physical
// RGB bus wiring on the SenseCap Indicator — resulting in a blank screen.
// These extra commands (ported from Meshtastic's LGFX_INDICATOR.h) apply the
// required vertical flip (MADCTL) and horizontal flip (SDIR) after the
// standard init sequence.
class Panel_SCIndicator : public lgfx::Panel_ST7701
{
public:
  const uint8_t *getInitCommands(uint8_t listno) const override
  {
    static constexpr const uint8_t list1[] = {
      0x36, 1, 0x10,                         // MADCTL: vertical flip
      0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10, // Command2 BK0 SEL
      0xC7, 1, 0x04,                         // SDIR: horizontal flip
      0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00, // Command2 BK0 DIS
      0xFF, 0xFF
    };
    switch (listno) {
      case 1: return list1;
      default: return lgfx::Panel_ST7701::getInitCommands(listno);
    }
  }
};

class LGFX : public lgfx::LGFX_Device
{
  Panel_SCIndicator _panel_instance;
  lgfx::Bus_RGB _bus_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_FT5x06 _touch_instance;

public:
  const uint16_t screenWidth = 480;
  const uint16_t screenHeight = 480;

  bool hasButton(void) { return true; }

  LGFX(void)
  {
    {
        auto cfg = _panel_instance.config();
        cfg.memory_width = 480;
        cfg.memory_height = 480;
        cfg.panel_width = screenWidth;
        cfg.panel_height = screenHeight;
        cfg.offset_x = 0;
        cfg.offset_y = 0;
        cfg.offset_rotation = 0;
        // pin_rst stays at default (no-connect). Upstream LovyanGFX cannot
        // toggle expander pins. The ST7701 RESX (TCA9535 bit 5) is pulsed
        // manually by sensecap_lcd_reset_pulse() in main.cpp before lcd.begin().
        _panel_instance.config(cfg);
    }

    {
        auto cfg = _panel_instance.config_detail();
        cfg.pin_cs  = 4 | IO_EXPANDER;  // TCA9535 bit 4 = ST7701 chip-select
        cfg.pin_sclk = 41;
        cfg.pin_mosi = 48;
        cfg.use_psram = 1;
        _panel_instance.config_detail(cfg);
    }

    {
        auto cfg = _bus_instance.config();
        cfg.panel = &_panel_instance;

        cfg.freq_write = 6000000;  // 6 MHz — matches Meshtastic LGFX_INDICATOR
        cfg.pin_henable = 18;

        cfg.pin_pclk = 21;
        cfg.pclk_active_neg = 0;
        cfg.pclk_idle_high = 0;
        cfg.de_idle_high = 1;

        cfg.pin_hsync = 16;
        cfg.hsync_polarity = 0;
        cfg.hsync_front_porch = 10;
        cfg.hsync_pulse_width = 8;
        cfg.hsync_back_porch = 50;

        cfg.pin_vsync = 17;
        cfg.vsync_polarity = 0;
        cfg.vsync_front_porch = 10;
        cfg.vsync_pulse_width = 8;
        cfg.vsync_back_porch = 20;

        cfg.pin_d0 = 15;
        cfg.pin_d1 = 14;
        cfg.pin_d2 = 13;
        cfg.pin_d3 = 12;
        cfg.pin_d4 = 11;
        cfg.pin_d5 = 10;
        cfg.pin_d6 = 9;
        cfg.pin_d7 = 8;
        cfg.pin_d8 = 7;
        cfg.pin_d9 = 6;
        cfg.pin_d10 = 5;
        cfg.pin_d11 = 4;
        cfg.pin_d12 = 3;
        cfg.pin_d13 = 2;
        cfg.pin_d14 = 1;
        cfg.pin_d15 = 0;

        _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);

    {
        auto cfg = _light_instance.config();
        cfg.pin_bl = 45;
        _light_instance.config(cfg);
    }
    _panel_instance.light(&_light_instance);

    {
        auto cfg = _touch_instance.config();
        cfg.pin_cs = GPIO_NUM_NC;
        cfg.x_min = 0;
        cfg.x_max = 479;
        cfg.y_min = 0;
        cfg.y_max = 479;
        cfg.pin_int = GPIO_NUM_NC; // do NOT use IO_EXPANDER for touch pins
        cfg.pin_rst = GPIO_NUM_NC; // do NOT use IO_EXPANDER for touch pins
        cfg.bus_shared = false;
        cfg.offset_rotation = 2;  // 180° — mirrors Meshtastic bbct.setOrientation(180,480,480)

        cfg.i2c_port = 0;
        cfg.i2c_addr = 0x48;
        cfg.pin_sda = 39;
        cfg.pin_scl = 40;
        cfg.freq = 400000;
        _touch_instance.config(cfg);
        _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

class SCIndicatorDisplay : public LGFXDisplay {
  LGFX disp;
public:
  SCIndicatorDisplay() : LGFXDisplay(480, 480, disp) {}
};
