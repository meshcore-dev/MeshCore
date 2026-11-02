#pragma once

#include "MicroNMEALocationProvider.h"
#include "RakGpsDriver.h"

class RakLocationProvider : public LocationProvider {
    RakGpsDriver& _driver;
    MicroNMEALocationProvider _nmea;
    uint32_t _gpsPeriodSeconds = 0;
    unsigned long _nextAcquisition = 0;
    unsigned long _acquisitionStarted = 0;
    bool _acquiringPeriodically = false;

public:
    RakLocationProvider(RakGpsDriver& driver, mesh::RTCClock* clock = nullptr)
        : _driver(driver), _nmea(driver.nmeaDriver(), clock) {}

    void syncTime() override {
        LocationProvider::syncTime();
        if (!_driver.usesRak12500()) _nmea.syncTime();
    }

    long getLatitude() override {
        return _driver.usesRak12500() ? _driver.getLatitude() : _nmea.getLatitude();
    }

    long getLongitude() override {
        return _driver.usesRak12500() ? _driver.getLongitude() : _nmea.getLongitude();
    }

    long getAltitude() override {
        return _driver.usesRak12500() ? _driver.getAltitude() : _nmea.getAltitude();
    }

    long satellitesCount() override {
        return _driver.usesRak12500() ? _driver.getSatellites() : _nmea.satellitesCount();
    }

    bool isValid() override {
        return _driver.usesRak12500() ? _driver.hasFix() : _nmea.isValid();
    }

    long getTimestamp() override {
        return _driver.usesRak12500() ? _driver.getTimestamp() : _nmea.getTimestamp();
    }

    void sendSentence(const char* sentence) override {
        if (!_driver.usesRak12500()) _nmea.sendSentence(sentence);
    }

    bool waitFor(const char* prefix, uint32_t timeout_ms) override {
        return _driver.usesRak12500() ? false : _nmea.waitFor(prefix, timeout_ms);
    }

    void drain() override {
        if (!_driver.usesRak12500()) _nmea.drain();
    }

    void reset() override {
        _driver.reset();
    }

    void begin() override {
        if (_driver.isDetected() && _driver.usesRak12500()) {
            _driver.begin();
            _acquiringPeriodically = _gpsPeriodSeconds > 0;
            _acquisitionStarted = millis();
        } else if (_driver.isDetected()) {
            _nmea.begin();
        } else {
            _driver.begin();
        }
    }

    void stop() override {
        _acquiringPeriodically = false;
        if (_driver.usesRak12500()) {
            _driver.stop();
        } else {
            _nmea.stop();
        }
    }

    bool detect(uint32_t timeout_ms) override {
        if (!_driver.detect(timeout_ms)) return false;

        if (_driver.usesRak12500()) {
            _acquiringPeriodically = _gpsPeriodSeconds > 0;
            _acquisitionStarted = millis();
        } else {
            _nmea.setGpsPeriod(_gpsPeriodSeconds);
            _nmea.begin();
        }
        return true;
    }

    bool keepEnabledAfterDetection() const override {
#ifdef FORCE_GPS_ALIVE
        return true;
#else
        return false;
#endif
    }

    void loop() override {
        if (_driver.usesRak12500()) {
            if (_gpsOn && _gpsPeriodSeconds > 0) {
                if (!_acquiringPeriodically &&
                    (long)(millis() - _nextAcquisition) >= 0) {
                    _driver.begin();
                    _acquiringPeriodically = true;
                    _acquisitionStarted = millis();
                }
                if (_acquiringPeriodically && isValid()) {
                    _driver.stop();
                    _acquiringPeriodically = false;
                    _nextAcquisition = millis() + _gpsPeriodSeconds * 1000UL;
                } else if (_acquiringPeriodically &&
                           (uint32_t)(millis() - _acquisitionStarted) >=
                               GPS_PERIODIC_ACQUISITION_TIMEOUT_MS) {
                    _driver.stop();
                    _acquiringPeriodically = false;
                    _nextAcquisition = millis() + _gpsPeriodSeconds * 1000UL;
                }
            }
        } else {
            _nmea.loop();
        }
    }

    bool isEnabled() override {
        return _driver.isEnabled();
    }

    void setGpsPeriod(uint32_t seconds) override {
        _gpsPeriodSeconds = seconds;
        _nextAcquisition = millis();
        if (!_driver.usesRak12500()) _nmea.setGpsPeriod(seconds);
    }

    uint32_t getGpsPeriod() const override {
        return _gpsPeriodSeconds;
    }
};
