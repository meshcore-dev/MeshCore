#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

MeshtinyBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager();

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
  MomentaryButton user_btn(ENCODER_PRESS, 1000, true, true);
  MomentaryButton joystick_left(ENCODER_LEFT, 1000, true, true);
  MomentaryButton joystick_right(ENCODER_RIGHT, 1000, true, true);
  MomentaryButton back_btn(PIN_SIDE_BUTTON, 1000, true, true);
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
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

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
