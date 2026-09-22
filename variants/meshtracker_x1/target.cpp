#include <Arduino.h>
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>

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
  if (gps.start()) {
    // Do not report the previous session's fix as a new fix after waking.
    _nmea->syncTime();
  }
}

void MeshTrackerX1SensorManager::sleep_gps() {
  gps.requestSleep();
}

void MeshTrackerX1SensorManager::prepareForShutdown() {
  gps.shutdown();
}

bool MeshTrackerX1SensorManager::begin() {
  // init GPS
  Serial1.begin(GPS_BAUD_RATE);

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

  gps.loop();
  _nmea->loop();

  if (millis() > next_gps_update) {
    if (gps.isActive() && _nmea->isValid()) {
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
    return gps.isActive() ? "1" : "0";
  }
  return NULL;
}
bool MeshTrackerX1SensorManager::setSettingValue(const char* name, const char* value) {
#ifdef X1_GPS_DIAGNOSTICS
  // Diagnostic builds only: volatile GNSS output configuration, never PAIR513.
  if (strcmp(name, "gps_diag") == 0 && gps.isActive()) {
    if (strcmp(value, "satellites") == 0) {
      _nmea->sendSentence("$PAIR062,2,1");
      _nmea->sendSentence("$PAIR062,3,1");
      _nmea->sendSentence("$PAIR021");
    } else if (strcmp(value, "quiet") == 0) {
      _nmea->sendSentence("$PAIR062,2,0");
      _nmea->sendSentence("$PAIR062,3,0");
    } else if (strcmp(value, "reset") == 0) {
      digitalWrite(GPS_RESET, HIGH);
      delay(10);
      digitalWrite(GPS_RESET, LOW);
      _nmea->syncTime();
    } else {
      return false;
    }
    return true;
  }
#endif
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      sleep_gps(); // sleep for faster fix !
    } else {
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}
