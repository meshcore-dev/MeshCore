#include "LocationProvider.h"
#include <Arduino.h>

#ifdef GPS_UBLOX_I2C
  #include "UbloxI2CGpsDriver.h"
#endif
#include "MicroNMEAGpsDriver.h"

const long LOOP_THROTTLE_MS = 1000;

GpsDriver* LocationProvider::detectDriver() {
  GpsDriver* driver = nullptr;

#ifdef GPS_UBLOX_I2C
  // Try u-blox I2C GPS first (if supported on this platform)
  MESH_DEBUG_PRINTLN("Trying u-blox I2C GPS...");
  driver = new UbloxI2CGpsDriver();
  if (driver->init()) {
    MESH_DEBUG_PRINTLN("u-blox I2C GPS initialized");
    return driver;
  }
  delete driver;
#endif

  // Try serial NMEA GPS
  MESH_DEBUG_PRINTLN("Trying NMEA GPS...");
  driver = new MicroNMEAGpsDriver(Serial1);
  if (driver->init()) {
    MESH_DEBUG_PRINTLN("NMEA GPS initialized");
    return driver;
  }
  delete driver;

  MESH_DEBUG_PRINTLN("No GPS detected");
  return nullptr;
}

bool LocationProvider::begin() {
  if (!_driver) {
    _detected = false;
    return false;
  }
  _detected = true;
  _hardware_on = true;

  #ifndef FORCE_GPS_ALIVE
  stopHardware();
  #endif

  return _detected;
}

void LocationProvider::startHardware() {
  _hardware_on = true;
  _driver->begin();
  _driver->reset();
}

void LocationProvider::stopHardware() {
  MESH_DEBUG_PRINTLN("Stopping GPS");
  _hardware_on = false;
  _driver->stop();
}

bool LocationProvider::validateReading(long lat, long lon, int sats) const {
  // Basic sanity check: non-zero coordinates and minimum satellites
  return (lat != 0 && lon != 0 && sats >= _min_satellites);
}

bool LocationProvider::hasValidFix() const {
  return validateReading(
    _driver->getLatitude(),
    _driver->getLongitude(),
    _driver->satellitesCount()
  );  
}

bool LocationProvider::updateLocation() {
  if (!_hardware_on) return false;

  // Get raw data from provider
  long lat = _driver->getLatitude();
  long lon = _driver->getLongitude();
  long alt = _driver->getAltitude();
  int sats = _driver->satellitesCount();

  // Validate reading against heuristics
  if (!validateReading(lat, lon, sats)) {
    MESH_DEBUG_PRINTLN("GPS: invalid reading - lat=%ld lon=%ld sats=%d", lat, lon, sats);
    return false;
  }

  // Convert to decimal degrees and meters
  node_lat = ((double)lat) / 1000000.;
  node_lon = ((double)lon) / 1000000.;
  node_altitude = ((double)alt) / 1000.0;
  node_timestamp = _driver->getTimestamp();

  MESH_DEBUG_PRINTLN("GPS: valid fix - lat=%f lon=%f alt=%f sats=%d",
    node_lat, node_lon, node_altitude, sats);

  // Sync RTC clock with GPS time
  if (_clock != nullptr) {
    _clock->setCurrentTime((uint32_t)node_timestamp);
  }

  return true;
}

void LocationProvider::syncTime() {
  // Force immediate GPS reading by resetting the cycle timer
  if (_detected && _enabled) {
    _next_cycle = 0;  // Trigger next cycle immediately
    MESH_DEBUG_PRINTLN("GPS sync requested - forcing immediate read");
  }
}

void LocationProvider::loop() {
  _driver->sync();

  long now = millis();
  if (now - _last_loop_run < LOOP_THROTTLE_MS) {
    return;
  }
  _last_loop_run = now;

  if (!_detected) {
    return;
  }

  if (!_enabled) {
    if (_hardware_on) {
      stopHardware();
    }
    return;
  }

  #ifdef FORCE_GPS_ALIVE
    if (!_hardware_on) {
      startHardware();
    }
    if (millis() > _next_cycle) {
      MESH_DEBUG_PRINTLN("GPS polling (always alive mode)");
      updateLocation();
      _next_cycle = millis() + _cycle_interval_ms;
    }
    return;
  #endif

  // GPS duty cycle management
  if (millis() > _next_cycle && !_reading_phase) {
    MESH_DEBUG_PRINTLN("Starting GPS cycle - waking GPS");
    startHardware();
    _wake_time = millis();
    _reading_phase = true;
    _next_cycle = millis() + _cycle_interval_ms;
  }

  if (_reading_phase) {
    bool got_valid_fix = updateLocation();
    bool timeout_reached = millis() > _wake_time + _wake_timeout_ms;

    if (got_valid_fix || timeout_reached) {
      if (got_valid_fix) {
        MESH_DEBUG_PRINTLN("GPS fix obtained - putting GPS to sleep");
      } else {
        MESH_DEBUG_PRINTLN("GPS timeout reached - putting GPS to sleep");
      }
      stopHardware();
      _reading_phase = false;
    }
  }
}
