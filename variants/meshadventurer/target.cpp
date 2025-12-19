#include <Arduino.h>
#include "target.h"

#include <helpers/sensors/MicroNMEALocationProvider.h>

MeshadventurerBoard board;

static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1);
MASensorManager sensors = MASensorManager(nmea);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
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

void MASensorManager::start_gps() {
  if(!gps_active) {
    MESH_DEBUG_PRINTLN("starting GPS");
    gps_active = true;
  }
}

void MASensorManager::stop_gps() {
  if(gps_active) {
    MESH_DEBUG_PRINTLN("stopping GPS");
    gps_active = false;
  }
}

bool MASensorManager::begin() {
  Serial1.setPins(PIN_GPS_RX, PIN_GPS_TX);
  Serial1.begin(9600);
  delay(500);
  return true;
}

bool MASensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if(requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void MASensorManager::loop() {
  static long next_gps_update = 0;
  _location->loop();
  if(millis() > next_gps_update && gps_active) {
    if(_location->isValid()) {
      node_lat = ((double)_location->getLatitude()) / 1000000.;
      node_lon = ((double)_location->getLongitude()) / 1000000.;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
    }
    next_gps_update = millis() + 1000;
  }
}

int MASensorManager::getNumSettings() const { return 1; }  // just one supported: "gps" (power switch)

const char* MASensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}
const char* MASensorManager::getSettingValue(int i) const {
  if(i == 0) {
    return gps_active ? "1" : "0";
  }
  return NULL;
}
bool MASensorManager::setSettingValue(const char* name, const char* value) {
  if(strcmp(name, "gps") == 0) {
    if(strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}
