#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

HeltecMeshPocket board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

SSD1680Display display;
MeshPocketSensorManager sensors = MeshPocketSensorManager();

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

bool radio_init() {
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
  return mesh::LocalIdentity(&rng);  // create new random identity
}

bool MeshPocketSensorManager::begin() {
  return true;
}

void MeshPocketSensorManager::loop() {

}

bool MeshPocketSensorManager::querySensors(uint8_t requester_permission, CayenneLPP& telemetry) {
  return true;
}

int MeshPocketSensorManager::getNumSettings() const {
  return 0;
}

const char* MeshPocketSensorManager::getSettingName(int i) const {
  return NULL;
}

const char* MeshPocketSensorManager::getSettingValue(int i) const {
  return NULL;
}

bool MeshPocketSensorManager::setSettingValue(const char* name, const char* value) {
  return false;
}
