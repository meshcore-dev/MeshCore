#pragma once
#include "LocationProvider.h"
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

class RAK12500LocationProvider : public LocationProvider {
  SFE_UBLOX_GNSS _gnss;
  long _lat = 0, _lng = 0, _alt = 0, _epoch = 0;
  int _sats = 0;
  bool _fix = false;
  long _next_log = 0;

public:
  RAK12500LocationProvider(mesh::RTCClock* clock = NULL, RefCountedDigitalPin* peripher_power = NULL)
    : LocationProvider(clock, peripher_power) {
  }

  bool detect() override {
    powerOn();
    delay(500);
    bool found = configureGNSS();
    if (found) {
      MESH_DEBUG_PRINTLN("RAK12500 GPS detected");
    } else {
      MESH_DEBUG_PRINTLN("RAK12500 GPS not found");
    }
    powerOff();
    return found;
  }

  void begin() override {
    if (!_powered) {
      MESH_DEBUG_PRINTLN("RAK12500: begin (cold start)");
      powerOn();
      delay(500);
      configureGNSS();
    } else {
      MESH_DEBUG_PRINTLN("RAK12500: begin (already enabled)");
    }
    _active = true;
  }

  void stop() override {
    MESH_DEBUG_PRINTLN("RAK12500: stop");
    _active = false;
    powerOff();
  }

  long getLatitude() override { return _lat; }
  long getLongitude() override { return _lng; }
  long getAltitude() override { return _alt; }
  long satellitesCount() override { return _sats; }
  bool isValid() override { return _fix; }
  long getTimestamp() override { return _epoch; }

protected:
  void pollLocation() override {
    _sats = _gnss.getSIV(2);
    if (_gnss.getGnssFixOk(8)) {
      _fix = true;
      _lat = _gnss.getLatitude(2) / 10;
      _lng = _gnss.getLongitude(2) / 10;
      _alt = _gnss.getAltitude(2);
    } else {
      _fix = false;
    }
    _epoch = _gnss.getUnixEpoch(2);

    if (millis() > _next_log) {
      _next_log = millis() + 5000;
      if (_fix) {
        MESH_DEBUG_PRINTLN("RAK12500: fix, sats=%d, lat=%ld, lng=%ld", _sats, _lat, _lng);
      } else {
        MESH_DEBUG_PRINTLN("RAK12500: no fix, sats=%d", _sats);
      }
    }
  }

private:
  // Try I2C connection and configure GNSS. Returns true if module responds.
  bool configureGNSS() {
    if (!_gnss.begin(Wire)) return false;
    _gnss.setI2COutput(COM_TYPE_UBX);
    _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_GPS);
    _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_GALILEO);
    _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_GLONASS);
    _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_SBAS);
    // Only 3 GNSS constellations can be enabled at the same time
    _gnss.enableGNSS(false, SFE_UBLOX_GNSS_ID_BEIDOU);
    _gnss.enableGNSS(false, SFE_UBLOX_GNSS_ID_IMES);
    _gnss.enableGNSS(false, SFE_UBLOX_GNSS_ID_QZSS);
    _gnss.setMeasurementRate(1000);
    _gnss.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT);
    return true;
  }
};
