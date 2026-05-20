#ifndef LGFX_h
#define LGFX_h

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

#ifndef SEEED_SENSECAP_INDICATOR
class LGFX : public lgfx::LGFX_Device
{
public:

  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;
  #if defined(ELECROW_DISPLAY_35)
  LGFX(void)
  {
    {                                      
      auto cfg = _bus_instance.config(); 

      cfg.port = 0;              
      cfg.freq_write = 80000000; 
      cfg.pin_wr = GPIO_NUM_18;
      cfg.pin_rd = GPIO_NUM_48;
      cfg.pin_rs = GPIO_NUM_45;

      cfg.pin_d0 = GPIO_NUM_47;
      cfg.pin_d1 = GPIO_NUM_21;
      cfg.pin_d2 = GPIO_NUM_14;
      cfg.pin_d3 = GPIO_NUM_13;
      cfg.pin_d4 = GPIO_NUM_12;
      cfg.pin_d5 = GPIO_NUM_11;
      cfg.pin_d6 = GPIO_NUM_10;
      cfg.pin_d7 = GPIO_NUM_9;
      cfg.pin_d8 = GPIO_NUM_3;
      cfg.pin_d9 = GPIO_NUM_8;
      cfg.pin_d10 = GPIO_NUM_16;
      cfg.pin_d11 = GPIO_NUM_15;
      cfg.pin_d12 = GPIO_NUM_7;
      cfg.pin_d13 = GPIO_NUM_6;
      cfg.pin_d14 = GPIO_NUM_5;
      cfg.pin_d15 = GPIO_NUM_4;

      _bus_instance.config(cfg);              
      _panel_instance.setBus(&_bus_instance); 
    }
    {                                        
      auto cfg = _panel_instance.config();

      cfg.pin_cs = -1;   
      cfg.pin_rst = -1;  
      cfg.pin_busy = -1; 

      cfg.memory_width = 320;   
      cfg.memory_height = 480;  
      cfg.panel_width = 320;    
      cfg.panel_height = 480;   
      cfg.offset_x = 0;         
      cfg.offset_y = 0;         
      cfg.offset_rotation = 0;  
      cfg.dummy_read_pixel = 8; 
      cfg.dummy_read_bits = 1;  
      cfg.readable = true;      
      cfg.invert = false;     
      cfg.rgb_order = false;    
      cfg.dlen_16bit = true;    
      cfg.bus_shared = true;    

      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }

  #elif defined(ELECROW_DISPLAY_50)
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      
      cfg.pin_d0  = GPIO_NUM_8; // B0
      cfg.pin_d1  = GPIO_NUM_3;  // B1
      cfg.pin_d2  = GPIO_NUM_46;  // B2
      cfg.pin_d3  = GPIO_NUM_9;  // B3
      cfg.pin_d4  = GPIO_NUM_1;  // B4
      
      cfg.pin_d5  = GPIO_NUM_5;  // G0
      cfg.pin_d6  = GPIO_NUM_6; // G1
      cfg.pin_d7  = GPIO_NUM_7;  // G2
      cfg.pin_d8  = GPIO_NUM_15;  // G3
      cfg.pin_d9  = GPIO_NUM_16; // G4
      cfg.pin_d10 = GPIO_NUM_4;  // G5
      
      cfg.pin_d11 = GPIO_NUM_45; // R0
      cfg.pin_d12 = GPIO_NUM_48; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_21; // R3
      cfg.pin_d15 = GPIO_NUM_14; // R4

      cfg.pin_henable = GPIO_NUM_40;
      cfg.pin_vsync   = GPIO_NUM_41;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;
      cfg.freq_write  = 15000000;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch  = 43;
      
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch  = 12;

      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      _bus_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width  = 800;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
  #elif defined(ELECROW_DISPLAY_70)
  // Constructor for the LGFX class.
  LGFX(void) {
    // Configure the RGB bus.
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      // Configure data pins.
      cfg.pin_d0  = GPIO_NUM_15; // B0
      cfg.pin_d1  = GPIO_NUM_7;  // B1
      cfg.pin_d2  = GPIO_NUM_6;  // B2
      cfg.pin_d3  = GPIO_NUM_5;  // B3
      cfg.pin_d4  = GPIO_NUM_4;  // B4
      
      cfg.pin_d5  = GPIO_NUM_9;  // G0
      cfg.pin_d6  = GPIO_NUM_46; // G1
      cfg.pin_d7  = GPIO_NUM_3;  // G2
      cfg.pin_d8  = GPIO_NUM_8;  // G3
      cfg.pin_d9  = GPIO_NUM_16; // G4
      cfg.pin_d10 = GPIO_NUM_1;  // G5
      
      cfg.pin_d11 = GPIO_NUM_14; // R0
      cfg.pin_d12 = GPIO_NUM_21; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_48; // R3
      cfg.pin_d15 = GPIO_NUM_45; // R4

      // Configure sync and clock pins.
      cfg.pin_henable = GPIO_NUM_41;
      cfg.pin_vsync   = GPIO_NUM_40;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;
      cfg.freq_write  = 15000000;

      // Configure timing parameters for horizontal and vertical sync.
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch  = 40;
      
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch  = 13;

      // Configure polarity for clock and data transmission.
      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      // Apply configuration to the RGB bus instance.
      _bus_instance.config(cfg);
    }

    // Configure the panel.
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width   = 800;
      cfg.panel_height  = 480;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;

      // Apply configuration to the panel instance.
      _panel_instance.config(cfg);
    }

    // Set the RGB bus and panel instances.
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
  #elif defined(SEEED_SENSECAP_INDICATOR)

  #else
    #error "No Display size defined!"
  #endif  
  
};
#endif
#endif