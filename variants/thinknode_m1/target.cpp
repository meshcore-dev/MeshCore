#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>
#include <helpers/sensors/NmeaGpsDriver.h>

ThinkNodeM1Board board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
NmeaGpsDriver gps_driver(Serial1);
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(gps_driver, &rtc_clock);
ThinkNodeM1SensorManager sensors = ThinkNodeM1SensorManager(nmea);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
  MomentaryButton backlight_btn(BACKLIGHT_BTN, 1000, true);
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

void ThinkNodeM1SensorManager::start_gps() {
  _location->setGpsOn(true);
}

void ThinkNodeM1SensorManager::stop_gps() {
  _location->setGpsOn(false);
}

bool ThinkNodeM1SensorManager::begin() {
  start_gps();
  return true;
}

bool ThinkNodeM1SensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void ThinkNodeM1SensorManager::loop() {
  static long next_gps_update = 0;

  _location->loop();

  if (millis() > next_gps_update) {
    if (_location->isGpsOn() && _location->hasValidFix()) {
      node_lat = ((double)_location->getLatitude())/1000000.;
      node_lon = ((double)_location->getLongitude())/1000000.;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
    }
    next_gps_update = millis() + 1000;
  }
}

int ThinkNodeM1SensorManager::getNumSettings() const {
  return 1;  // always show GPS setting
}

const char* ThinkNodeM1SensorManager::getSettingName(int i) const {
  return (i == 0) ? "gps" : NULL;
}

const char* ThinkNodeM1SensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return _location->isGpsOn() ? "1" : "0";
  }
  return NULL;
}

bool ThinkNodeM1SensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}
