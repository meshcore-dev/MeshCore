#include <Arduino.h>
#include "target.h"
#include "variant.h"

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
#endif

ESP32Board board;

SPIClass vspi(VSPI);  // LoRa and Eth share VSPI

#if defined(P_LORA_SCLK)
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, vspi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  HammerSensorManager sensors = HammerSensorManager(nmea);
#else
  HammerSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(BUTTON_PIN);
#endif

#ifdef HAS_ETHERNET
  SerialEthernetInterface eth_interface;
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);

#if defined(P_LORA_SCLK)
  vspi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  if (!radio.std_init(&vspi)) return false;
#else
  if (!radio.std_init()) return false;
#endif

#ifdef HAS_ETHERNET
  // Generate unique MAC from ESP32 chip ID
  uint64_t chipid = ESP.getEfuseMac();
  uint8_t mac[6];
  mac[0] = 0xDE; mac[1] = 0xAD;
  mac[2] = (chipid >> 32) & 0xFF;
  mac[3] = (chipid >> 24) & 0xFF;
  mac[4] = (chipid >> 16) & 0xFF;
  mac[5] = (chipid >> 8)  & 0xFF;
  // vspi already initialized above — pass it to share the bus
  if (eth_interface.begin(vspi, ETH_TCP_PORT, ETH_CS_PIN, ETH_RST_PIN, mac)) {
    eth_interface.enable();
  }
#endif

  return true;
}

uint32_t radio_get_rng_seed() { return radio.random(0x7FFFFFFF); }

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) { radio.setOutputPower(dbm); }

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}

void HammerSensorManager::start_gps() {
  if (!gps_active) {
    _location->begin();
    gps_active = true;
  }
}

void HammerSensorManager::stop_gps() {
  if (gps_active) {
    gps_active = false;
    _location->stop();
  }
}

bool HammerSensorManager::begin() {
  Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  return true;
}

bool HammerSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void HammerSensorManager::loop() {
  static long next_gps_update = 0;

  if (gps_active) {
    _location->loop();
  }

  if (millis() > next_gps_update) {
    if (gps_active && _location->isValid()) {
      node_lat = ((double)_location->getLatitude()) / 1000000.0;
      node_lon = ((double)_location->getLongitude()) / 1000000.0;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
    }
    next_gps_update = millis() + 1000;
  }
}

int HammerSensorManager::getNumSettings() const {
  return 1;
}

const char* HammerSensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}

const char* HammerSensorManager::getSettingValue(int i) const {
  return i == 0 ? (gps_active ? "1" : "0") : NULL;
}

bool HammerSensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
  return false;
}
