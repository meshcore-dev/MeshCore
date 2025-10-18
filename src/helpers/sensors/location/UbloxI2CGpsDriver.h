#pragma once

#include "GpsDriver.h"
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <Wire.h>

#define UBLOX_I2C_GPS_ADDRESS 0x42  // u-blox GPS default I2C address

class UbloxI2CGpsDriver : public GpsDriver {
private:
  SFE_UBLOX_GNSS _gnss;
  long _lat = 0;
  long _lng = 0;
  long _alt = 0;
  int _sats = 0;
  long _epoch = 0;
  bool _initialized = false;
  int _enable_pin = -1;

  bool probePin(uint8_t ioPin) {
    pinMode(ioPin,OUTPUT);
    digitalWrite(ioPin,LOW);
    delay(500);
    digitalWrite(ioPin,HIGH);
    delay(500);

    if (_gnss.begin(Wire) == true) {
      MESH_DEBUG_PRINTLN("u-blox I2C GPS found on pin %i", ioPin);
      _enable_pin = ioPin;
      _initialized = true;
      return true;
    }
    return false;
  }

public:
  UbloxI2CGpsDriver() {}

  bool init() override {
    // Try different sockets on RAK base boards for I2C GPS
    if (probePin(WB_IO2)) return true;      // Socket A
    if (probePin(WB_IO4)) return true;      // Socket C
    if (probePin(WB_IO5)) return true;      //  Socket F

    MESH_DEBUG_PRINTLN("No u-blox I2C GPS found");
    return false;
  }

  // Wake GPS
  void begin() override {
    if (_enable_pin != -1) {

      //set initial waking state
      pinMode(_enable_pin,OUTPUT);
      digitalWrite(_enable_pin,LOW);
      delay(500);
      digitalWrite(_enable_pin,HIGH);
      delay(500);

      if (_gnss.begin(Wire) != true) {
        MESH_DEBUG_PRINTLN("u-blox I2C GPS not responding!");
        _initialized = false;
        return;
      }

      // Route UBX on the port we actually use (I2C)
      _gnss.setPortOutput(COM_PORT_I2C, COM_TYPE_UBX);
      _gnss.setI2COutput(COM_TYPE_UBX);

      // Make sure NAV-PVT will actually arrive
      _gnss.setNavigationFrequency(1);
      _gnss.setAutoPVT(true);

      // Keep receiver awake while debugging
      _gnss.powerSaveMode(false);
      _gnss.setAutoNAVSAT(true);          // enable auto NAV-SAT parsing


    }
  }
  void reset() override {
    // U-blox doesn't need explicit reset via this interface
  }

  // Stop GPS (pull enable pin LOW)
  void stop() override {
    if (_enable_pin != -1) {
      pinMode(_enable_pin, OUTPUT);
      digitalWrite(_enable_pin, LOW);
    }
  }

  void sync() override {
    if (!_initialized) return;

    if (_gnss.getPVT(500)) {
      _lat = _gnss.getLatitude() / 10;
      _lng = _gnss.getLongitude() / 10;
      _alt = _gnss.getAltitude();
      _sats = _gnss.getSIV();
      _epoch = _gnss.getUnixEpoch();
    }
  }

  long getLatitude() override { return _lat; }
  long getLongitude() override { return _lng; }
  long getAltitude() override { return _alt; }
  long satellitesCount() override { return _sats; }
  long getTimestamp() override { return _epoch; }
  void sendSentence(const char* sentence) override {}
  bool isEnabled() override { return _initialized; }

  SFE_UBLOX_GNSS* getGnss() { return &_gnss; }
};
