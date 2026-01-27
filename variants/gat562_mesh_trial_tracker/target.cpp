#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

GAT562MeshTrialTrackerBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
#if ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors = EnvironmentSensorManager();
#endif

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;

MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);

#ifdef JOYSTICK_UP
MomentaryButton joystick_up(JOYSTICK_UP, 1000, true);
#endif

#ifdef JOYSTICK_DOWN
MomentaryButton joystick_down(JOYSTICK_DOWN, 1000, true);
#endif

#ifdef JOYSTICK_LEFT
MomentaryButton joystick_left(JOYSTICK_LEFT, 1000, true);
#endif

#ifdef JOYSTICK_RIGHT
MomentaryButton joystick_right(JOYSTICK_RIGHT, 1000, true);
#endif

#ifdef JOYSTICK_ENTER
MomentaryButton joystick_enter(JOYSTICK_ENTER, 1000, true);
#endif

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
