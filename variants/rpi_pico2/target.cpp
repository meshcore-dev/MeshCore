#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

RpiPico2Board board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI1);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

#ifndef LORA_CR
#define LORA_CR 5
#endif

bool radio_init() {
  rtc_clock.begin(Wire);

  return radio.std_init(&SPI1);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}

