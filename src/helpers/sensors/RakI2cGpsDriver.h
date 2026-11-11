#pragma once

#include "GpsDriver.h"

#include <SparkFun_u-blox_GNSS_Arduino_Library.h>

// Driver for the RAK12500 (u-blox, I2C). Serial NMEA modules are handled by NmeaGpsDriver.
class RakI2cGpsDriver : public GpsDriver {
    TwoWire& _wire;
    SFE_UBLOX_GNSS _gnss;
    uint8_t _powerPin = 0;
    bool _detected = false;
    bool _started = false;

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
            return true;
        }

#ifndef RAK_3401
        pinMode(powerPin, INPUT);
#endif
        return false;
    }

public:
    RakI2cGpsDriver(TwoWire& wire = Wire) : _wire(wire) {}

    void begin() override {
        if (!_detected) return;
#ifndef RAK_3401
        pinMode(_powerPin, OUTPUT);
        digitalWrite(_powerPin, HIGH);
#endif
        _started = true;
    }

    void reset() override {}

    void stop() override {
        if (_detected) {
#ifndef RAK_3401
            pinMode(_powerPin, OUTPUT);
            digitalWrite(_powerPin, LOW);
#endif
        }
        _started = false;
    }

    bool isActive() const override {
        return _started;
    }

    // Probes the I2C module on the possible power pins; the module is left powered.
    bool detect(uint32_t) override {
        if (_detected) return true;

        if (probeRak12500(WB_IO2) || probeRak12500(WB_IO4) || probeRak12500(WB_IO5)) {
            _detected = true;
            _started = true;
        }
        return _detected;
    }

    bool isDetected() const { return _detected; }

    bool hasFix() { return _started && _gnss.getGnssFixOk(8); }
    long getLatitude() { return _gnss.getLatitude(2) / 10; }
    long getLongitude() { return _gnss.getLongitude(2) / 10; }
    long getAltitude() { return _gnss.getAltitude(2); }
    long getSatellites() { return _gnss.getSIV(2); }
    long getTimestamp() { return _gnss.getUnixEpoch(2); }
};
