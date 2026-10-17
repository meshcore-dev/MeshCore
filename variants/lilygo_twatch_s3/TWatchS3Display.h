#pragma once

#include <helpers/ui/LGFXDisplay.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ST7789 240x240 on SPI3 + PWM backlight (GPIO 45) + FT5x06/FT6x36 capacitive
// touch on a separate I2C bus (Wire1: SDA 39 / SCL 40).
class LGFX_TWatchS3 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_FT5x06  _touch_instance;

public:
  LGFX_TWatchS3(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host   = SPI3_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = true;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 18;
      cfg.pin_mosi = 13;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 38;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs   = 12;
      cfg.pin_rst  = -1;
      cfg.pin_busy = -1;
      // ST7789 GRAM is 240x320; memory_height must be 320 so the 80px rotation
      // offset is applied (otherwise a strip of the panel shows noise).
      cfg.memory_width   = 240;
      cfg.memory_height  = 320;
      cfg.panel_width    = 240;
      cfg.panel_height   = 240;
      cfg.offset_x       = 0;
      cfg.offset_y       = 0;
      cfg.offset_rotation = 1;
      cfg.readable   = false;
      cfg.invert     = true;
      cfg.rgb_order  = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl      = 45;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    {
      auto cfg = _touch_instance.config();
      cfg.x_min      = 0;
      cfg.x_max      = 239;
      cfg.y_min      = 0;
      cfg.y_max      = 239;
      cfg.pin_int    = 16;
      cfg.pin_rst    = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 2;  // touch IC mounted 180deg vs LCD's horizontal axis
      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda  = 39;
      cfg.pin_scl  = 40;
      cfg.freq     = 400000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};

class TWatchS3Display : public LGFXDisplay {
  LGFX_TWatchS3 disp;
public:
  TWatchS3Display() : LGFXDisplay(240, 240, disp) {}
};
