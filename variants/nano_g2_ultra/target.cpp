#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>
#include "NanoG2UltraGpsDriver.h"

NanoG2Ultra board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
// NanoG2UltraGpsDriver now handles the special init needed for NanoG2
NanoG2UltraGpsDriver gps_driver(Serial1);
MicroNMEALocationProvider nmea(gps_driver, &rtc_clock);
NanoG2UltraSensorManager sensors = NanoG2UltraSensorManager(nmea, gps_driver);

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

void NanoG2UltraSensorManager::start_gps() {
  MESH_DEBUG_PRINTLN("Starting GPS");
  _location->setGpsOn(true);
}

void NanoG2UltraSensorManager::stop_gps() {
  MESH_DEBUG_PRINTLN("Stopping GPS");
  _location->setGpsOn(false);
}

bool NanoG2UltraSensorManager::begin() {
  MESH_DEBUG_PRINTLN("Checking GPS switch state");

  // Check initial switch state to determine if GPS should be active
  if (_gps_driver->detectAtBoot()) {
    MESH_DEBUG_PRINTLN("GPS was on at boot, GPS enabled");
    start_gps();
  } else {
    MESH_DEBUG_PRINTLN("GPS was not on at boot, GPS disabled");
  }

  return true;
}

bool NanoG2UltraSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP &telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) { // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void NanoG2UltraSensorManager::loop() {
  static long next_gps_update = 0;

  if (!_location->isGpsOn()) {
    return; // GPS is not active, skip further processing
  }

  _location->loop();

  if (millis() > next_gps_update) {
    if (_location->isGpsOn() && _location->hasValidFix()) {
      node_lat = ((double)_location->getLatitude()) / 1000000.;
      node_lon = ((double)_location->getLongitude()) / 1000000.;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      MESH_DEBUG_PRINTLN("VALID location: lat %f lon %f", node_lat, node_lon);
    } else {
      MESH_DEBUG_PRINTLN("INVALID location, waiting for fix");
    }
    MESH_DEBUG_PRINTLN("GPS satellites: %d", _location->satellitesCount());
    next_gps_update = millis() + 1000;
  }
}

int NanoG2UltraSensorManager::getNumSettings() const {
  return 1;
} // just one supported: "gps" (power switch)

const char *NanoG2UltraSensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}

const char *NanoG2UltraSensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return _location->isGpsOn() ? "1" : "0";
  }
  return NULL;
}

bool NanoG2UltraSensorManager::setSettingValue(const char *name, const char *value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
  return false; // not supported
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}