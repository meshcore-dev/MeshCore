#include <Arduino.h>
#include "target.h"

#include <helpers/sensors/MicroNMEALocationProvider.h>

HeltecV3Board board;

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
// GPS_EN (GPIO35) drives N-ch MOSFET → P-ch high-side switch; GPS_RESET (GPIO36) active LOW
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock, GPS_RESET, GPS_EN, &board.periph_power);
HWTSensorManager sensors = HWTSensorManager(nmea);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display(&board.periph_power);   // peripheral power pin is shared
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  
#if defined(P_LORA_SCLK)
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif

}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

void HWTSensorManager::start_gps() {
  if (!gps_active) {
    _location->begin();  // Claims periph_power via RefCountedDigitalPin
    gps_active = true;

    if (powersaving_enabled && _location->isPowerSavingEnabled()) {
      gps_wake = true;           // gps_active is true
      _location->syncTime();     // Clear GPS data and force sync time
      _location->setNextSleep(); // Next time to off
    }

    Serial1.println("$CFGSYS,h35155*68");  // Configure GPS for all constellations
  }
}

void HWTSensorManager::stop_gps() {
  if (gps_active) {
    if (powersaving_enabled && _location->isPowerSavingEnabled()) {
      gps_wake = false;          // gps_active is unchanged (true) even the GPS sleep (e.g: off)
      _location->stopTimeSync(); // Stop time sync
      _location->setNextWake();  // Next time to on
    } else {
      gps_active = false;
      gps_wake = false; // When GPS is off, wake is false to be sure
    }

    _location->stop();  // Releases periph_power via RefCountedDigitalPin
  }
}

bool HWTSensorManager::begin() {
  // init GPS port
  Serial1.begin(115200, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  return true;
}

bool HWTSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void HWTSensorManager::loop() {
  static long next_gps_update = 0;

  // PowerSaving
  if (powersaving_enabled) {
    if (_location->isPowerSavingEnabled()) {
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

int HWTSensorManager::getNumSettings() const { return 1; }  // just one supported: "gps" (power switch)

const char* HWTSensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}
const char* HWTSensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return gps_active ? "1" : "0";
  }
  return NULL;
}
bool HWTSensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
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
