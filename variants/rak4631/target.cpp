#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

RAK4631Board board;

#ifndef PIN_USER_BTN
  #define PIN_USER_BTN (-1)
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);

  #if defined(PIN_USER_BTN_ANA)
  MomentaryButton analog_btn(PIN_USER_BTN_ANA, 1000, 20);
  #endif
#endif

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/RakLocationProvider.h>
  RakI2cGpsDriver gps_driver;
  #ifdef GPS_RAIL_POWER_CONTROL
  #ifdef ETHERNET_ENABLED
    #error "GPS_RAIL_POWER_CONTROL would cut the RAK13800's power, which also comes from WB_IO2"
  #endif
  // WB_IO2 switches 3V3_S, which powers the serial GPS and every other slot module.
  RefCountedDigitalPin gps_rail(WB_IO2);
  RakLocationProvider location(gps_driver, &rtc_clock, Serial1, &gps_rail);
  #else
  RakLocationProvider location(gps_driver, &rtc_clock);
  #endif
  EnvironmentSensorManager sensors(location);
#else
  EnvironmentSensorManager sensors;
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
