#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#if ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
#endif

ThinkNodeM1Board board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
#if ENV_INCLUDE_GPS
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
#endif
ThinkNodeM1SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  rtc_clock.begin(Wire);

  #if ENV_INCLUDE_GPS
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

bool ThinkNodeM1SensorManager::begin() {
  #if ENV_INCLUDE_GPS
  detectLocationProvider();
  if (!_location) return true;

  // Initialize GPS switch pin
  pinMode(PIN_GPS_SWITCH, INPUT);
  last_gps_switch_state = digitalRead(PIN_GPS_SWITCH);

  // Initialize GPS power pin
  pinMode(GPS_EN, OUTPUT);

  // Check initial switch state to determine if GPS should be active
  if (last_gps_switch_state == HIGH) {  // Switch is HIGH when ON
    start_gps();
  }
  #endif

  return true;
}

#if ENV_INCLUDE_GPS
void ThinkNodeM1SensorManager::loop() {
  static long last_switch_check = 0;
  if (!_location) return;

  // Check GPS switch state every second
  if (millis() - last_switch_check > 1000) {
    bool current_switch_state = digitalRead(PIN_GPS_SWITCH);

    // Detect switch state change
    if (current_switch_state != last_gps_switch_state) {
      last_gps_switch_state = current_switch_state;

      if (current_switch_state == HIGH) {  // Switch is ON
        MESH_DEBUG_PRINTLN("GPS switch ON");
        start_gps();
      } else {  // Switch is OFF
        MESH_DEBUG_PRINTLN("GPS switch OFF");
        stop_gps();
      }
    }

    last_switch_check = millis();
  }

  updateGpsCoordinates();
}
#endif
