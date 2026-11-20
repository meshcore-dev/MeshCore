#pragma once

#include <helpers/ui/LGFXDisplay.h>

class Chatter2Panel : public lgfx::LGFX_Device {
  lgfx::Panel_ST7735S _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _backlight;

public:
  Chatter2Panel() {
    {
      auto cfg = _panel.config();
      cfg.pin_cs = -1;
      cfg.pin_rst = 15;
      cfg.memory_width = 128;
      cfg.memory_height = 160;
      cfg.panel_width = 128;
      cfg.panel_height = 160;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.invert = false;
      cfg.rgb_order = false;
      _panel.config(cfg);
    }

    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.pin_sclk = 27;
      cfg.pin_mosi = 26;
      cfg.pin_miso = -1;
      cfg.pin_dc = 33;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);

    {
      auto cfg = _backlight.config();
      cfg.pin_bl = 32;
      _backlight.config(cfg);
    }
    _panel.light(&_backlight);

    setPanel(&_panel);
  }
};

class Chatter2Display : public LGFXDisplay {
  Chatter2Panel panel;

public:
  Chatter2Display() : LGFXDisplay(160, 128, panel) {}

  bool begin() {
    turnOn();
    display->init();
    display->setRotation(1);
    display->setBrightness(64);
    display->setColorDepth(8);
    display->setTextColor(TFT_WHITE);

    buffer.setColorDepth(8);
    return buffer.createSprite(width(), height()) != nullptr;
  }
};
