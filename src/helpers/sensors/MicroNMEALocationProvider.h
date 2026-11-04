#pragma once

#include "NmeaGpsDriver.h"
#include "LocationProvider.h"
#include <MicroNMEA.h>
#include <RTClib.h>
#include <string.h>

#ifndef GPS_PERIODIC_ACQUISITION_TIMEOUT_MS
    #define GPS_PERIODIC_ACQUISITION_TIMEOUT_MS 300000UL
#endif

// in periodic mode, keep reading after the first valid fix until enough satellites
// are used or the settle time has elapsed
#ifndef GPS_MIN_SATS_FOR_FIX
    #define GPS_MIN_SATS_FOR_FIX 5
#endif
#ifndef GPS_FIX_SETTLE_MS
    #define GPS_FIX_SETTLE_MS 10000UL
#endif

class MicroNMEALocationProvider : public LocationProvider {
    char _nmeaBuffer[100];
    MicroNMEA nmea;
    mesh::RTCClock* _clock;
    NmeaGpsDriver* _driver;
    bool _ownsDriver = false;
    unsigned long next_check = 0;
    long time_valid = 0;
    unsigned long _last_time_sync = 0;
    uint32_t _gpsPeriodSeconds = 0;
    unsigned long _nextAcquisition = 0;
    unsigned long _acquisitionStarted = 0;
    unsigned long _firstFixInWindow = 0;
    bool _acquiringPeriodically = false;
    static const unsigned long TIME_SYNC_INTERVAL = 1800000;

    uint32_t _uart_bytes = 0;
    uint32_t _nmea_checksum_ok = 0;
    uint32_t _nmea_checksum_bad = 0;
    uint32_t _begin_calls = 0;
    uint32_t _stop_calls = 0;
    unsigned long _last_uart_ms = 0;
    unsigned long _last_nmea_ms = 0;
    bool _uart_seen = false;
    bool _nmea_seen = false;

    static uint32_t ageMs(unsigned long timestamp) {
        return (uint32_t)(millis() - timestamp);
    }

    static void formatAge(char* out, size_t out_size, bool seen, unsigned long timestamp) {
        if (!seen) {
            snprintf(out, out_size, "never");
        } else {
            snprintf(out, out_size, "%lu", (unsigned long)ageMs(timestamp));
        }
    }

    void startPeriodicAcquisition() {
        nmea.clear();
        time_valid = 0;
        _driver->begin();
        _driver->reset();
        _acquiringPeriodically = true;
        _acquisitionStarted = millis();
        _firstFixInWindow = 0;
    }

    void finishPeriodicAcquisition() {
        if (_firstFixInWindow == 0) fixStatsMiss();
        _driver->stop();
        _acquiringPeriodically = false;
        _nextAcquisition = millis() + _gpsPeriodSeconds * 1000UL;
    }

    void saveFix() {
        LastFix fix;
        fix.latitude = nmea.getLatitude();
        fix.longitude = nmea.getLongitude();
        nmea.getAltitude(fix.altitude);
        fix.satellites = nmea.getNumSatellites();
        fix.timestamp = getTimestamp();
        fix.acquiredAt = millis();
        fix.valid = true;
        _lastFix = fix;
    }

    void updateTime() {
        if (!isValid()) time_valid = 0;

        if ((long)(millis() - next_check) <= 0) return;
        next_check = millis() + 1000;

        if (!_time_sync_needed && _clock != nullptr &&
            (millis() - _last_time_sync) > TIME_SYNC_INTERVAL) {
            _time_sync_needed = true;
        }
        if (_time_sync_needed && time_valid > 2 && _clock != nullptr) {
            _clock->setCurrentTime(getTimestamp());
            _time_sync_needed = false;
            _last_time_sync = millis();
        }
        if (isValid()) time_valid++;
    }

public:
    MicroNMEALocationProvider(NmeaGpsDriver& driver, mesh::RTCClock* clock = nullptr)
        : nmea(_nmeaBuffer, sizeof(_nmeaBuffer)), _clock(clock), _driver(&driver) {}

    // Temporary migration adapter for targets and downstream boards. New code
    // should inject a GpsDriver; this constructor will be removed once targets
    // have been migrated.
    MicroNMEALocationProvider(HardwareSerial& serial, mesh::RTCClock* clock = nullptr,
                              int pin_reset = GPS_RESET, int pin_en = GPS_EN,
                              RefCountedDigitalPin* peripher_power = nullptr)
        : nmea(_nmeaBuffer, sizeof(_nmeaBuffer)), _clock(clock),
          _driver(new NmeaGpsDriver(serial, PIN_GPS_TX, PIN_GPS_RX, pin_reset, pin_en,
                                    peripher_power)),
          _ownsDriver(true) {}

    MicroNMEALocationProvider(Stream& serial, mesh::RTCClock* clock = nullptr,
                              int pin_reset = GPS_RESET, int pin_en = GPS_EN,
                              RefCountedDigitalPin* peripher_power = nullptr)
        : nmea(_nmeaBuffer, sizeof(_nmeaBuffer)), _clock(clock),
          _driver(new NmeaGpsDriver(serial, PIN_GPS_TX, PIN_GPS_RX, pin_reset, pin_en,
                                    peripher_power)),
          _ownsDriver(true) {}

    ~MicroNMEALocationProvider() override {
        if (_ownsDriver) delete _driver;
    }

    void begin() override {
        _begin_calls++;
        const bool wasActive = _driver->isActive();
        _driver->begin();
        if (!wasActive) {
            _acquisitionStarted = millis();
            _firstFixInWindow = 0;
        }
        if (_gpsPeriodSeconds > 0 && _driver->isActive()) {
            nmea.clear();
            time_valid = 0;
            _acquiringPeriodically = true;
            _acquisitionStarted = millis();
            _firstFixInWindow = 0;
        }
    }

    void reset() override {
        _driver->reset();
    }

    void stop() override {
        _stop_calls++;
        _acquiringPeriodically = false;
        _driver->stop();
    }

    bool isActive() override {
        return _driver->isActive();
    }

    bool hasHardwareEnableState() const override {
        return _driver->hasHardwareEnableState();
    }

    bool isHardwareEnabled() const override {
        return _driver->isHardwareEnabled();
    }

    bool detect(uint32_t timeout_ms) override {
        return _driver->detect(timeout_ms);
    }

    void setGpsPeriod(uint32_t seconds) override {
        _gpsPeriodSeconds = seconds;
        _nextAcquisition = millis();
        _acquiringPeriodically = false;
    }

    uint32_t getGpsPeriod() const override {
        return _gpsPeriodSeconds;
    }

    void syncTime() override {
        nmea.clear();
        LocationProvider::syncTime();
    }

    long getLatitude() override {
        return nmea.isValid() ? nmea.getLatitude() : _lastFix.latitude;
    }
    long getLongitude() override {
        return nmea.isValid() ? nmea.getLongitude() : _lastFix.longitude;
    }
    long getAltitude() override {
        if (!nmea.isValid()) return _lastFix.altitude;
        long alt = 0;
        nmea.getAltitude(alt);
        return alt;
    }
    long satellitesCount() override {
        return nmea.isValid() ? nmea.getNumSatellites() : _lastFix.satellites;
    }
    bool isValid() override { return nmea.isValid(); }

    long getTimestamp() override {
        if (!nmea.isValid()) return _lastFix.timestamp;
        DateTime dt(nmea.getYear(), nmea.getMonth(), nmea.getDay(),
                    nmea.getHour(), nmea.getMinute(), nmea.getSecond());
        return dt.unixtime();
    }

    void drain() override { _driver->drain(); }
    const char* getSentence() const { return nmea.getSentence(); }

    void sendSentence(const char* sentence) override {
        _driver->sendNmeaSentence(sentence);
    }

    bool waitFor(const char* prefix, uint32_t timeout_ms) override {
        const size_t prefixLength = strlen(prefix);
        const uint32_t deadline = millis() + timeout_ms;
        while ((int32_t)(millis() - deadline) < 0) {
            if (_driver->available() > 0) {
                const char c = (char)_driver->read();
#ifdef GPS_NMEA_DEBUG
                Serial.print(c);
#endif
                if (nmea.process(c) && strncmp(nmea.getSentence(), prefix, prefixLength) == 0) {
                    return true;
                }
            } else {
                yield();
            }
        }
        return false;
    }

    void loop() override {
        if (_driver->pollGpsSwitch()) _gpsOn = true;

        if (_gpsOn && _gpsPeriodSeconds > 0 && !_acquiringPeriodically &&
            (long)(millis() - _nextAcquisition) >= 0) {
            startPeriodicAcquisition();
        }

        while (_driver->available() > 0) {
            const char c = (char)_driver->read();
            _uart_bytes++;
            _last_uart_ms = millis();
            _uart_seen = true;
#ifdef GPS_NMEA_DEBUG
            Serial.print(c);
#endif
            const bool parsed = nmea.process(c);

            if ((c == '\0' || c == '\r' || c == '\n') && nmea.getSentence()[0] != '\0') {
                if (MicroNMEA::testChecksum(nmea.getSentence())) {
                    _nmea_checksum_ok++;
                    _last_nmea_ms = millis();
                    _nmea_seen = true;

                    const char* messageId = nmea.getMessageID();
                    if (parsed && nmea.isValid() &&
                        (strcmp(messageId, "GGA") == 0 || strcmp(messageId, "RMC") == 0)) {
                        saveFix();
                        if (_firstFixInWindow == 0) {
                            _firstFixInWindow = millis();
                            fixStatsFix((uint32_t)(_firstFixInWindow - _acquisitionStarted));
                        }
                        if (_acquiringPeriodically) {
                            if (nmea.getNumSatellites() >= GPS_MIN_SATS_FOR_FIX ||
                                (uint32_t)(millis() - _firstFixInWindow) >= GPS_FIX_SETTLE_MS) {
                                finishPeriodicAcquisition();
                            }
                        }
                    }
                } else {
                    _nmea_checksum_bad++;
                }
            }
        }

        // no timeout until a first fix has been obtained (cold start can be long)
        if (_acquiringPeriodically && _lastFix.valid &&
            (uint32_t)(millis() - _acquisitionStarted) >= GPS_PERIODIC_ACQUISITION_TIMEOUT_MS) {
            finishPeriodicAcquisition();
        }

        updateTime();
    }

    void formatDiagnostics(char* out, size_t out_size) override {
        if (out_size == 0) return;

        char uart_age[11];
        char nmea_age[11];
        char fix_age[11];
        formatAge(uart_age, sizeof(uart_age), _uart_seen, _last_uart_ms);
        formatAge(nmea_age, sizeof(nmea_age), _nmea_seen, _last_nmea_ms);
        formatAge(fix_age, sizeof(fix_age), _lastFix.valid, _lastFix.acquiredAt);

        snprintf(out, out_size,
            "en:%u ub:%lu ua:%s ok:%lu bad:%lu na:%s sat:%ld fix:%u fa:%s bc:%lu sc:%lu",
            isActive() ? 1U : 0U,
            (unsigned long)_uart_bytes,
            uart_age,
            (unsigned long)_nmea_checksum_ok,
            (unsigned long)_nmea_checksum_bad,
            nmea_age,
            satellitesCount(),
            isValid() ? 1U : 0U,
            fix_age,
            (unsigned long)_begin_calls,
            (unsigned long)_stop_calls);
    }
};
