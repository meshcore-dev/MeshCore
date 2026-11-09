#pragma once

#include "MicroNMEALocationProvider.h"
#include "NmeaGpsDriver.h"
#include "RakI2cGpsDriver.h"

class RakLocationProvider : public LocationProvider {
    RakI2cGpsDriver& _driver;       // RAK12500 (I2C)
    NmeaGpsDriver _nmeaDriver;      // serial NMEA module
    MicroNMEALocationProvider _nmea;
    bool _detected = false;
    bool _useI2c = false;
    uint32_t _gpsPeriodSeconds = 0;
    unsigned long _nextAcquisition = 0;
    unsigned long _acquisitionStarted = 0;
    bool _acquiringPeriodically = false;

    void saveRak12500Fix() {
        LastFix fix;
        fix.latitude = _driver.getLatitude();
        fix.longitude = _driver.getLongitude();
        fix.altitude = _driver.getAltitude();
        fix.satellites = _driver.getSatellites();
        fix.timestamp = _driver.getTimestamp();
        fix.acquiredAt = millis();
        fix.valid = true;
        _lastFix = fix;
    }

public:
    // serialPower, if given, switches the serial module's supply (WB_IO2 on WisBlock bases).
    RakLocationProvider(RakI2cGpsDriver& driver, mesh::RTCClock* clock = nullptr,
                        UART_TYPE& serial = Serial1, RefCountedDigitalPin* serialPower = nullptr)
        : _driver(driver), _nmeaDriver(serial, PIN_GPS_TX, PIN_GPS_RX, GPS_RESET, GPS_EN, serialPower),
          _nmea(_nmeaDriver, clock) {}

    void syncTime() override {
        LocationProvider::syncTime();
        if (!_useI2c) _nmea.syncTime();
    }

    long getLatitude() override {
        return _useI2c ? _lastFix.latitude : _nmea.getLatitude();
    }

    long getLongitude() override {
        return _useI2c ? _lastFix.longitude : _nmea.getLongitude();
    }

    long getAltitude() override {
        return _useI2c ? _lastFix.altitude : _nmea.getAltitude();
    }

    long satellitesCount() override {
        return _useI2c ? _lastFix.satellites : _nmea.satellitesCount();
    }

    bool isValid() override {
        return _useI2c ? _driver.hasFix() : _nmea.isValid();
    }

    FixStats getFixStats() const override {
        return _useI2c ? _fixStats : _nmea.getFixStats();
    }

    LastFix getLastFix() const override {
        return _useI2c ? _lastFix : _nmea.getLastFix();
    }

    long getTimestamp() override {
        return _useI2c ? _lastFix.timestamp : _nmea.getTimestamp();
    }

    // the anti-spoofing guard (GPS_GUARD) only covers the serial NMEA path
    GpsGuard* getGuard() override {
        return _useI2c ? nullptr : _nmea.getGuard();
    }

    bool hasValidFix() override {
        return _useI2c ? LocationProvider::hasValidFix() : _nmea.hasValidFix();
    }

    bool getClockOffset(long& offset) override {
        return _useI2c ? false : _nmea.getClockOffset(offset);
    }

    bool applyTimeNow() override {
        return _useI2c ? false : _nmea.applyTimeNow();
    }

    void sendSentence(const char* sentence) override {
        if (!_useI2c) _nmea.sendSentence(sentence);
    }

    bool waitFor(const char* prefix, uint32_t timeout_ms) override {
        return _useI2c ? false : _nmea.waitFor(prefix, timeout_ms);
    }

    void drain() override {
        if (!_useI2c) _nmea.drain();
    }

    void reset() override {
        if (!_useI2c) _nmea.reset();
    }

    void begin() override {
        if (_detected && _useI2c) {
            _driver.begin();
            _acquiringPeriodically = _gpsPeriodSeconds > 0;
            _acquisitionStarted = millis();
        } else if (_detected) {
            _nmea.begin();
        } else {
            _nmeaDriver.begin();
        }
    }

    void stop() override {
        _acquiringPeriodically = false;
        if (_useI2c) {
            _driver.stop();
        } else {
            _nmea.stop();
        }
    }

    bool detect(uint32_t timeout_ms) override {
        if (_detected) return true;

        // the serial module is started first, then the I2C module is probed
        _nmeaDriver.begin();
        if (_driver.detect(timeout_ms)) {
            // The serial driver keeps its claim on serialPower, so the rail stays as it is without one.
            // The RAK12500 driver switches its own power pin, and serialPower is only used on the serial path.
            _useI2c = true;
        } else if (!_nmeaDriver.detect(0)) {
            _nmeaDriver.stop();
            return false;
        }
        _detected = true;

        if (_useI2c) {
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
        if (_useI2c) {
            if (_gpsOn) {
                if (_gpsPeriodSeconds > 0) {
                    if (!_acquiringPeriodically &&
                        (long)(millis() - _nextAcquisition) >= 0) {
                        _driver.begin();
                        _acquiringPeriodically = true;
                        _acquisitionStarted = millis();
                    }
                }

                const bool hasCurrentFix = _driver.hasFix();
                if (hasCurrentFix) {
                    // first fix since the start
                    if (!_lastFix.valid || (long)(_lastFix.acquiredAt - _acquisitionStarted) < 0) {
                        fixStatsFix((uint32_t)(millis() - _acquisitionStarted));
                    }
                    saveRak12500Fix();
                }

                if (_gpsPeriodSeconds > 0 && _acquiringPeriodically && hasCurrentFix) {
                    _driver.stop();
                    _acquiringPeriodically = false;
                    _nextAcquisition = millis() + _gpsPeriodSeconds * 1000UL;
                } else if (_gpsPeriodSeconds > 0 && _acquiringPeriodically && _lastFix.valid &&
                           (uint32_t)(millis() - _acquisitionStarted) >=
                               GPS_PERIODIC_ACQUISITION_TIMEOUT_MS) {
                    fixStatsMiss();
                    _driver.stop();
                    _acquiringPeriodically = false;
                    _nextAcquisition = millis() + _gpsPeriodSeconds * 1000UL;
                }
            }
        } else {
            _nmea.loop();
        }
    }

    bool isActive() override {
        return _useI2c ? _driver.isActive() : _nmea.isActive();
    }

    void setGpsPeriod(uint32_t seconds) override {
        _gpsPeriodSeconds = seconds;
        _nextAcquisition = millis();
        if (!_useI2c) _nmea.setGpsPeriod(seconds);
    }

    uint32_t getGpsPeriod() const override {
        return _gpsPeriodSeconds;
    }

    bool isGpsOn() const override {
        return _useI2c ? _gpsOn : _nmea.isGpsOn();
    }

    uint32_t getAcquisitionAge() const override {
        return _useI2c ? (uint32_t)(millis() - _acquisitionStarted) / 1000UL : _nmea.getAcquisitionAge();
    }

    bool startOnce() override {
        return _useI2c ? false : _nmea.startOnce();   // not supported on the RAK12500
    }

    void setGpsOn(bool on) override {
        if (_useI2c) LocationProvider::setGpsOn(on);
        else { _gpsOn = on; _nmea.setGpsOn(on); }
    }
};
