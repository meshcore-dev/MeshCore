#pragma once

#include "NmeaGpsDriver.h"

#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

class RakGpsDriver : public GpsDriver {
    NmeaGpsDriver _serialDriver;
    TwoWire& _wire;
    SFE_UBLOX_GNSS _gnss;
    uint8_t _powerPin = 0;
    bool _usingRak12500 = false;
    bool _detected = false;

    bool probeRak12500(uint8_t powerPin) {
#if defined(ETHERNET_ENABLED) && defined(RAK_BOARD)
        if (powerPin == WB_IO2) return false;
#endif

#ifndef RAK_3401
        pinMode(powerPin, OUTPUT);
        digitalWrite(powerPin, LOW);
        delay(500);
        digitalWrite(powerPin, HIGH);
#endif
        delay(500);

        if (_gnss.begin(_wire)) {
            _gnss.setI2COutput(COM_TYPE_UBX);
            _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_GPS);
            _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_GALILEO);
            _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_GLONASS);
            _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_SBAS);
            _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_BEIDOU);
            _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_IMES);
            _gnss.enableGNSS(true, SFE_UBLOX_GNSS_ID_QZSS);
            _gnss.setMeasurementRate(1000);
            _gnss.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT);
            _powerPin = powerPin;
            _usingRak12500 = true;
            return true;
        }

#ifndef RAK_3401
        pinMode(powerPin, INPUT);
#endif
        return false;
    }

public:
    RakGpsDriver(HardwareSerial& serial = Serial1, TwoWire& wire = Wire)
        : _serialDriver(serial), _wire(wire) {}

    void begin() override {
        if (_usingRak12500) {
#ifndef RAK_3401
            pinMode(_powerPin, OUTPUT);
            digitalWrite(_powerPin, HIGH);
#endif
            return;
        }
        _serialDriver.begin();
    }

    void reset() override {
        if (!_usingRak12500) _serialDriver.reset();
    }

    void stop() override {
        if (_usingRak12500) {
#ifndef RAK_3401
            pinMode(_powerPin, OUTPUT);
            digitalWrite(_powerPin, LOW);
#endif
            return;
        }
        _serialDriver.stop();
    }

    bool isActive() const override {
        return _usingRak12500 ? true : _serialDriver.isActive();
    }

    bool detect(uint32_t) override {
        if (_detected) return true;

        _serialDriver.begin();
        if (probeRak12500(WB_IO2) || probeRak12500(WB_IO4) || probeRak12500(WB_IO5)) {
            _detected = true;
            return true;
        }

        if (_serialDriver.detect(0)) {
            _detected = true;
            return true;
        }

        _serialDriver.stop();
        return false;
    }

    bool usesRak12500() const { return _usingRak12500; }
    bool isDetected() const { return _detected; }
    NmeaGpsDriver& nmeaDriver() { return _serialDriver; }

    bool hasFix() { return _usingRak12500 && _gnss.getGnssFixOk(8); }
    long getLatitude() { return _gnss.getLatitude(2) / 10; }
    long getLongitude() { return _gnss.getLongitude(2) / 10; }
    long getAltitude() { return _gnss.getAltitude(2); }
    long getSatellites() { return _gnss.getSIV(2); }
    long getTimestamp() { return _gnss.getUnixEpoch(2); }
};
