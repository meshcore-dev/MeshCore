#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

MeshSolarBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1);
SolarSensorManager sensors = SolarSensorManager(nmea);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
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
  return mesh::LocalIdentity(&rng);  // create new random identity
}

void SolarSensorManager::start_gps() {
  if (!gps_active) {
    gps_active = true;
    _location->begin();
  }
}

void SolarSensorManager::stop_gps() {
  if (gps_active) {
    gps_active = false;
    _location->stop();
  }
}

bool SolarSensorManager::begin() {
  Serial1.begin(9600);

  // We'll consider GPS detected if we see any data on Serial1
  gps_detected = (Serial1.available() > 0);

  if (gps_detected) {
    MESH_DEBUG_PRINTLN("GPS detected");
  } else {
    MESH_DEBUG_PRINTLN("No GPS detected");
  }

  return true;
}

bool SolarSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void SolarSensorManager::loop() {
  static long next_gps_update = 0;

  _location->loop();

  if (millis() > next_gps_update) {
    if (_location->isValid()) {
      node_lat = ((double)_location->getLatitude())/1000000.;
      node_lon = ((double)_location->getLongitude())/1000000.;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
    }
    next_gps_update = millis() + 1000;
  }
}

int SolarSensorManager::getNumSettings() const {
  return gps_detected ? 1 : 0;  // only show GPS setting if GPS is detected
}

const char* SolarSensorManager::getSettingName(int i) const {
  return (gps_detected && i == 0) ? "gps" : NULL;
}

const char* SolarSensorManager::getSettingValue(int i) const {
  if (gps_detected && i == 0) {
    return gps_active ? "1" : "0";
  }
  return NULL;
}

bool SolarSensorManager::setSettingValue(const char* name, const char* value) {
  if (gps_detected && strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}
