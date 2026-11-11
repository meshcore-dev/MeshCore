#pragma once

#include "NmeaGpsDriver.h"
#include "LocationProvider.h"
#include <MicroNMEA.h>
#include <RTClib.h>
#include <string.h>
#ifdef GPS_GUARD
#include "GpsGuard.h"
#endif

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
    // MicroNMEA never writes past the length it is given, but its fixed-width field
    // skips can read a few bytes past the end of a malformed sentence. The zeroed tail
    // lets those reads stop on NUL bytes instead of reading the members that follow.
    static constexpr size_t NMEA_BUFFER_LEN = 100;
    static constexpr size_t NMEA_BUFFER_TAIL = 16;
    char _nmeaBuffer[NMEA_BUFFER_LEN + NMEA_BUFFER_TAIL] = {};
    MicroNMEA nmea;
    mesh::RTCClock* _clock;
    NmeaGpsDriver* _driver;
    unsigned long next_check = 0;
    long time_valid = 0;
    unsigned long _last_time_sync = 0;
    uint32_t _gpsPeriodSeconds = 0;
    unsigned long _nextAcquisition = 0;
    unsigned long _acquisitionStarted = 0;
    unsigned long _firstFixInWindow = 0;
    bool _acquiringPeriodically = false;
    unsigned long _onceDeadline = 0;
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
#ifdef GPS_GUARD
    GpsGuard _guard;
#endif

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
        nmea.clear();      // the parser keeps the last time as valid otherwise
        time_valid = 0;
        _acquiringPeriodically = false;
        _nextAcquisition = millis() + _gpsPeriodSeconds * 1000UL;
    }

    bool saveFix() {
        // only store a fix when the snapshot is coherent (sats > 0).
        // Returns true if the fix was stored.
        if (nmea.getNumSatellites() == 0) return false;
#ifdef GPS_GUARD
        // only plausible, stable fixes become the last fix (position, telemetry, time sync)
        if (!_guard.acceptFix(nmea.getLatitude(), nmea.getLongitude(), nmea.getNumSatellites(),
                              nmea.getHDOP(), getTimestamp())) return false;
#endif

        LastFix fix;
        fix.latitude = nmea.getLatitude();
        fix.longitude = nmea.getLongitude();
        // altitude comes from GGA only: keep the previous one if not available
        if (!nmea.getAltitude(fix.altitude)) {
            fix.altitude = _lastFix.valid ? _lastFix.altitude : 0;
        }
        fix.satellites = nmea.getNumSatellites();
        fix.timestamp = getTimestamp();
        fix.acquiredAt = millis();
        fix.valid = true;
        _lastFix = fix;
        return true;
    }

    void updateTime() {
        if (!isValid() || (millis() - _last_nmea_ms) > 2000) time_valid = 0;   // no stale time

        if ((long)(millis() - next_check) <= 0) return;
        next_check = millis() + 1000;

        if (!_time_sync_needed && _clock != nullptr &&
            (millis() - _last_time_sync) > TIME_SYNC_INTERVAL) {
            _time_sync_needed = true;
        }
        if (_time_sync_needed && time_valid > 2 && _clock != nullptr && timeSyncAllowed()) {
            _clock->setCurrentTime(getTimestamp());
            _time_sync_needed = false;
            _last_time_sync = millis();
        }
        if (isValid()) time_valid++;
    }

    bool timeSyncAllowed() {
#ifdef GPS_GUARD
        return _guard.acceptTime(getTimestamp(), _clock->getCurrentTime());
#else
        return true;
#endif
    }

public:
    MicroNMEALocationProvider(NmeaGpsDriver& driver, mesh::RTCClock* clock = nullptr)
        : nmea(_nmeaBuffer, NMEA_BUFFER_LEN), _clock(clock), _driver(&driver) {}

    void begin() override {
        _begin_calls++;
        _onceDeadline = 0;
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
        _onceDeadline = 0;
        _acquiringPeriodically = false;
        _driver->stop();
        nmea.clear();
        time_valid = 0;
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

    uint32_t getAcquisitionAge() const override {
        return (uint32_t)(millis() - _acquisitionStarted) / 1000UL;
    }

    bool startOnce() override {
        if (_gpsOn) { // already on, acquire now
            _nextAcquisition = millis();
            return true;
        }
        setGpsOn(true);
        _onceDeadline = millis() + GPS_PERIODIC_ACQUISITION_TIMEOUT_MS;
        return true;
    }

    void syncTime() override {
        nmea.clear();
        LocationProvider::syncTime();
    }

    // Position, altitude and satellite count describe the last saved fix
    // Before the first saved fix, fall back to the live parser state
    // (not with GPS_GUARD: an unverified position is never reported).
#ifdef GPS_GUARD
    GpsGuard* getGuard() override { return &_guard; }
    bool hasValidFix() override { return _lastFix.valid; }
    long getLatitude() override { return _lastFix.latitude; }
    long getLongitude() override { return _lastFix.longitude; }
    long getAltitude() override { return _lastFix.altitude; }
#else
    long getLatitude() override {
        return _lastFix.valid ? _lastFix.latitude : nmea.getLatitude();
    }
    long getLongitude() override {
        return _lastFix.valid ? _lastFix.longitude : nmea.getLongitude();
    }
    long getAltitude() override {
        if (_lastFix.valid) return _lastFix.altitude;
        long alt = 0;
        nmea.getAltitude(alt);
        return alt;
    }
#endif

    // GPS time minus clock time, from a current fix with a settled time
    bool getClockOffset(long& offset) override {
        if (_clock == nullptr || !isValid() || time_valid < 3) return false;
        offset = (long)getTimestamp() - (long)_clock->getCurrentTime();
        return true;
    }

    // write the current GPS time to the clock now (caller has done its own checks)
    bool applyTimeNow() override {
        if (_clock == nullptr || !isValid() || time_valid < 3) return false;
        _clock->setCurrentTime(getTimestamp());
        _time_sync_needed = false;
        _last_time_sync = millis();
        return true;
    }
    long satellitesCount() override {
        return _lastFix.valid ? _lastFix.satellites : nmea.getNumSatellites();
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

        if (_onceDeadline != 0) {
            if (_firstFixInWindow != 0 && (long)(_onceDeadline - (millis() + GPS_FIX_SETTLE_MS)) > 0) {
                _onceDeadline = millis() + GPS_FIX_SETTLE_MS;
            }
            if ((long)(millis() - _onceDeadline) >= 0) {
                if (_driver->isActive()) {
                    setGpsOn(false);
                } else { // do not stop twice
                    _gpsOn = false;
                    _onceDeadline = 0;
                    _acquiringPeriodically = false;
                }
            }
        }

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
#ifdef GPS_GUARD
                    if (parsed && !nmea.isValid() &&
                        (strcmp(messageId, "GGA") == 0 || strcmp(messageId, "RMC") == 0)) {
                        _guard.noFix(nmea.getNumSatellites());
                    }
#endif
                    if (parsed && nmea.isValid() &&
                        (strcmp(messageId, "GGA") == 0 || strcmp(messageId, "RMC") == 0)) {
                        if (!saveFix()) continue;  // incoherent snapshot
                        if (_firstFixInWindow == 0) {
                            _firstFixInWindow = millis();
                            fixStatsFix((uint32_t)(_firstFixInWindow - _acquisitionStarted));
                        }
                        if (_acquiringPeriodically) {
                            // stay on until a pending time sync is done (or the settle time is over)
                            const bool synced = _clock == nullptr || !_time_sync_needed;
                            if ((nmea.getNumSatellites() >= GPS_MIN_SATS_FOR_FIX && synced) ||
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
