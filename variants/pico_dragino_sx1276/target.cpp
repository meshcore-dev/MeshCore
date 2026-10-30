#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

PicoDraginoBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, SPI);
WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  MicroNMEALocationProvider nmea(Serial1, &rtc_clock);
  EnvironmentSensorManager sensors(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

bool radio_init() {
  rtc_clock.begin(Wire);

  SPI.setSCK(P_LORA_SCLK);
  SPI.setTX(P_LORA_MOSI);
  SPI.setRX(P_LORA_MISO);
  SPI.begin();

  //passing NULL skips init of SPI
  return radio.std_init(NULL);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
