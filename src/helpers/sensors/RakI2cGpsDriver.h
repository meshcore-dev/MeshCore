#pragma once

#include "GpsDriver.h"

#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include <RTClib.h>

// Driver for the RAK12500 (u-blox, I2C). Serial NMEA modules are handled by NmeaGpsDriver.
class RakI2cGpsDriver : public GpsDriver {
    TwoWire& _wire;
    SFE_UBLOX_GNSS _gnss;
    uint8_t _powerPin = 0;
    bool _detected = false;
    bool _started = false;
    bool _pvtSeen = false;   // a NAV-PVT has been received since boot

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
        if (powerPin == WB_IO2) {
            // WB_IO2 switches the 3V3_S slot rail, which also powers a serial GPS and the other
            // slot modules. Left as an input, the rail follows whatever the base board pulls it
            // to: some keep it on, others switch it off, and the serial GPS loses power (#3830).
            // Keep it driven HIGH, as before the probe; a GPS power pin re-claims it later.
            digitalWrite(powerPin, HIGH);
        } else {
            pinMode(powerPin, INPUT);
        }
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

    bool hasFix() {
        if (!_started) return false;
        // The library allocates its NAV-PVT record without clearing it: until the first reply has
        // arrived, the fix flag and every other field are whatever was on the heap.
        if (!_pvtSeen) {
            if (!_gnss.getPVT(8)) return false;
            _pvtSeen = true;
        }
        return _gnss.getGnssFixOk(8);
    }
    long getLatitude() { return _gnss.getLatitude(2) / 10; }
    long getLongitude() { return _gnss.getLongitude(2) / 10; }
    long getAltitude() { return _gnss.getAltitude(2); }
    long getSatellites() { return _gnss.getSIV(2); }
    // Not getUnixEpoch(): it indexes its day tables with the year and month reported by the module,
    // unchecked (year outside 2020..2099 or month outside 1..12 reads outside the tables).
    // Returns 0 when the date or time is not plausible; the caller then leaves the clock alone.
    long getTimestamp() {
        const uint16_t year = _gnss.getYear(2);
        const uint8_t month = _gnss.getMonth(2), day = _gnss.getDay(2);
        const uint8_t hour = _gnss.getHour(2), minute = _gnss.getMinute(2), second = _gnss.getSecond(2);
        if (year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
            hour > 23 || minute > 59 || second > 60) return 0;
        return DateTime(year, month, day, hour, minute, second).unixtime();
    }
};
