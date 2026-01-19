#pragma once

#include <helpers/ESP32Board.h>
#include <helpers/esp32/ESPNOWRadio.h>
#include <helpers/SensorManager.h>

extern ESP32Board board;
extern ESPNOWRadio radio_driver;
extern ESP32RTCClock rtc_clock;
extern SensorManager sensors;

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
