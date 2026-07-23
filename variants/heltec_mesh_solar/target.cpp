#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

MeshSolarBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
SolarSensorManager sensors = SolarSensorManager(nmea);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

void SolarSensorManager::start_gps() {
  if (!gps_active) {
    gps_active = true;

    if (powersaving_enabled && _location->isPowerSavingEnabled()) {
      gps_wake = true;       // gps_active is true
      _location->syncTime();     // Clear GPS data and force sync time
      _location->setNextSleep(); // Next time to off
    }

    _location->begin();
  }
}

void SolarSensorManager::stop_gps() {
  if (gps_active) {
    if (powersaving_enabled && _location->isPowerSavingEnabled()) {
      gps_wake = false;      // gps_active is unchanged (true) even the GPS sleep (e.g: off)
      _location->stopTimeSync(); // Stop time sync
      _location->setNextWake();  // Next time to on
    } else {
      gps_active = false;
      gps_wake = false; // When GPS is off, wake is false to be sure
    }

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

  // PowerSaving
  if (powersaving_enabled) {
    if (gps_detected && _location->isPowerSavingEnabled()) {
      if (gps_wake && ((int32_t)(millis() - _location->getNextSleep()) >= 0 ||
                       !_location->waitingTimeSync())) { // Time to off or GPS set
        if ((int32_t)(millis() - _location->getNextSleep()) >= 0) {
          POWERSAVING_DEBUG_PRINTLN("GPS wake timeout. Enter sleep");
        } else if (!_location->waitingTimeSync()) {
          POWERSAVING_DEBUG_PRINTLN("GPS set. Enter sleep early");
        }

        stop_gps();
      } else if (!gps_wake && ((int32_t)(millis() - _location->getNextWake()) >= 0)) { // Time to on
        POWERSAVING_DEBUG_PRINTLN("GPS sleep timeout. Wakeup.");

        start_gps();
      } else if (!gps_wake && _location->waitingTimeSync()) { // On for "gps sync"
        POWERSAVING_DEBUG_PRINTLN("CLI gps sync. Wakeup");

        start_gps();
      }
    }
  }

  if ((!powersaving_enabled && gps_active) || (powersaving_enabled && gps_wake)) {
    _location->loop();
  }

  if ((int32_t)(millis() - next_gps_update) >= 0) {
    if ((!powersaving_enabled && gps_active) || (powersaving_enabled && gps_wake)) {
      if (_location->isValid()) {
        node_lat = ((double)_location->getLatitude()) / 1000000.;
        node_lon = ((double)_location->getLongitude()) / 1000000.;
        MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
        node_altitude = ((double)_location->getAltitude()) / 1000.0;
        MESH_DEBUG_PRINTLN("lat %f lon %f alt %f", node_lat, node_lon, node_altitude);
      }

      // In powersaving mode, GPS is on and off. Only update data when GPS is on
      if (powersaving_enabled) next_gps_update = millis() + 1000;
    }

    if (!powersaving_enabled) next_gps_update = millis() + 1000;
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
      if (powersaving_enabled) {
        _location->enablePowerSaving(false);
      }

      stop_gps();
    } else {
      if (powersaving_enabled) {
        _location->enablePowerSaving(true);
      }

      start_gps();
    }
    return true;
  }
  return false;  // not supported
}
