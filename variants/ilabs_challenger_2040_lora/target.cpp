#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

ChallengerBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, SPI1);
WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

bool radio_init() {
  rtc_clock.begin(Wire);

  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);

  // std_init sets the SPI1 pins and begins the bus (the Waveshare variant does
  // that here and passes NULL). NSS stays a RadioLib-driven GPIO: don't give it
  // to SPI1.setCS(), CustomSX1276 notes that freezes.
  return radio.std_init(&SPI1);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
