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

#include <helpers/RefCountedDigitalPin.h>
RefCountedDigitalPin peripher_power(WB_IO2, HIGH);

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/L76KLocationProvider.h>
  L76KLocationProvider nmea = L76KLocationProvider(Serial1, &rtc_clock, GPS_RESET, GPS_EN, &peripher_power);
  #include <helpers/sensors/RAK12500LocationProvider.h>
  RAK12500LocationProvider rak12500 = RAK12500LocationProvider(&rtc_clock, &peripher_power);
#endif

EnvironmentSensorManager sensors;

bool radio_init() {
  rtc_clock.begin(Wire);

  peripher_power.begin();

  #if ENV_INCLUDE_GPS
  sensors.registerLocationProvider(&rak12500);
  sensors.registerLocationProvider(&nmea);
  #endif

  return radio.std_init(&SPI);
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(int8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
