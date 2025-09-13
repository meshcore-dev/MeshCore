#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

NanoG2Ultra board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1);
NanoG2UltraSensorManager sensors = NanoG2UltraSensorManager(nmea);

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
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

void NanoG2UltraSensorManager::start_gps() {
  MESH_DEBUG_PRINTLN("Starting GPS");
  if (!gps_active) {
    digitalWrite(PIN_GPS_STANDBY, HIGH); // Wake GPS from standby
    Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
    Serial1.begin(9600);
    MESH_DEBUG_PRINTLN("Waiting for gps to power up");
    delay(1000);
    gps_active = true;
  }
  _location->begin();
}

void NanoG2UltraSensorManager::stop_gps() {
  MESH_DEBUG_PRINTLN("Stopping GPS");
  if (gps_active) {
    digitalWrite(PIN_GPS_STANDBY, LOW); // sleep GPS
    gps_active = false;
  }
  _location->stop();
}

bool NanoG2UltraSensorManager::begin() {
  digitalWrite(PIN_GPS_STANDBY, HIGH); // Wake GPS from standby
  Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
  Serial1.begin(9600);
  MESH_DEBUG_PRINTLN("Checking GPS switch state");
  delay(1000);

  // Check initial switch state to determine if GPS should be active
  if (Serial1.available() > 0) {
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

  if (!gps_active) {
    return; // GPS is not active, skip further processing
  }

  _location->loop();

  if (millis() > next_gps_update) {
    if (_location->isValid()) {
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
    return gps_active ? "1" : "0";
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
