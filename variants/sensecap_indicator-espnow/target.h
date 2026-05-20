#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <helpers/ESP32Board.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/esp32/SenseCapHAL.h>           // TCA9535 IO expander HAL for RadioLib
#include <helpers/esp32/SenseCapSX1262Wrapper.h>  // DIO1-verified IRQ dispatch

#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

#ifdef ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
#endif

#ifdef DISPLAY_CLASS
  #include "SCIndicatorDisplay.h"
  #include <helpers/ui/MomentaryButton.h>
#endif


// -------------------------------------------------
// Board
// -------------------------------------------------
extern ESP32Board board;


// -------------------------------------------------
// Radio (SX1262 - SenseCAP uses SX1262)
// -------------------------------------------------
extern SenseCapSX1262Wrapper radio_driver;


// -------------------------------------------------
// RTC
// -------------------------------------------------
extern AutoDiscoverRTCClock rtc_clock;


// -------------------------------------------------
// Sensors
// -------------------------------------------------
extern EnvironmentSensorManager sensors;


// -------------------------------------------------
// Display + Button
// -------------------------------------------------
#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif


// -------------------------------------------------
// Shared I2C mutex
// Protects Wire access shared between SenseCapHAL (TCA9535 @ 0x20)
// and the LVGL touch callback (FT5x06 @ 0x48).
// Created in radio_init() before std_init(); use via g_i2c_mutex.
// -------------------------------------------------
extern SemaphoreHandle_t g_i2c_mutex;


// -------------------------------------------------
// Functions
// -------------------------------------------------
bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();