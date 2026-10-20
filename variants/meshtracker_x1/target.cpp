#include <Arduino.h>
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>
#include <helpers/sensors/AirohaSleep.h>

MeshTrackerX1Board board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock rtc_clock;
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
MeshTrackerX1SensorManager sensors = MeshTrackerX1SensorManager(nmea);

#ifdef DISPLAY_CLASS
  NullDisplayDriver display;
#endif

bool radio_init() {
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

void MeshTrackerX1SensorManager::start_gps() {
  gps_active = true;
  digitalWrite(GPS_EN, HIGH);
  delay(10);
  digitalWrite(GPS_RTC_INT, HIGH);
  delay(5);
  digitalWrite(GPS_RTC_INT, LOW);
}

void MeshTrackerX1SensorManager::stop_gps() {
  gps_active = false;
  digitalWrite(GPS_VRTC_EN, HIGH);   // keep GPS RTC alive for faster fix on wake
  digitalWrite(GPS_RTC_INT, LOW);    // make sure this is LOW so we can pulse it to wake
  airohaEnterSleep(_nmea);
  digitalWrite(GPS_EN, LOW);
}

bool MeshTrackerX1SensorManager::begin() {
  // init GPS
  Serial1.begin(GPS_BAUD_RATE);
  digitalWrite(GPS_RESET, HIGH);
  delay(10);
  digitalWrite(GPS_RESET, LOW);

  // init SPA06-003 barometer
  baro_ok = spa06.begin(SPA06_003_DEFAULT_ADDR, &Wire) || spa06.begin(0x76, &Wire);
  if (baro_ok) {
    spa06.setPressureOversampling(SPA06_003_OVERSAMPLE_8);
    spa06.setTemperatureOversampling(SPA06_003_OVERSAMPLE_8);
    // 1 Hz continuous keeps reads non-blocking at minimal power cost
    spa06.setPressureMeasureRate(SPA06_003_RATE_1);
    spa06.setTemperatureMeasureRate(SPA06_003_RATE_1);
    spa06.setMeasurementMode(SPA06_003_MEAS_CONTINUOUS_BOTH);
  }
  return true;
}

bool MeshTrackerX1SensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  if (requester_permissions & TELEM_PERM_ENVIRONMENT && baro_ok) {
    telemetry.addTemperature(TELEM_CHANNEL_SELF, spa06.readTemperature());
    telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, spa06.readPressure());
  }
  return true;
}

void MeshTrackerX1SensorManager::loop() {
  static long next_gps_update = 0;


  _nmea->loop();

  if (millis() > next_gps_update) {
    if (gps_active && _nmea->isValid()) {
      node_lat = ((double)_nmea->getLatitude())/1000000.;
      node_lon = ((double)_nmea->getLongitude())/1000000.;
      node_altitude = ((double)_nmea->getAltitude()) / 1000.0;
    }
    next_gps_update = millis() + 1000;
  }
}

int MeshTrackerX1SensorManager::getNumSettings() const { return 1; }  // just one supported: "gps" (power switch)

const char* MeshTrackerX1SensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}
const char* MeshTrackerX1SensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return gps_active ? "1" : "0";
  }
  return NULL;
}
bool MeshTrackerX1SensorManager::setSettingValue(const char* name, const char* value) {
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
