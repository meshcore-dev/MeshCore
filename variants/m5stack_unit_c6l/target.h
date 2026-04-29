#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <M5StackUnitC6LBoard.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>

#ifdef DISPLAY_CLASS
  #include <helpers/ui/SSD1306SPIDisplay.h>
  extern DISPLAY_CLASS display;
#endif

#if defined(P_LORA_SCLK)
  extern SPIClass spi;
  extern bool spi_initialized;
#endif

extern M5StackUnitC6LBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
