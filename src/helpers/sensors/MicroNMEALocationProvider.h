#pragma once

#include "LocationProvider.h"
#include <MicroNMEA.h>
#include <RTClib.h>
#include <helpers/RefCountedDigitalPin.h>
#include <string.h>

#ifndef GPS_EN
    #ifdef PIN_GPS_EN
        #define GPS_EN PIN_GPS_EN
    #else
        #define GPS_EN (-1)
    #endif
#endif

#ifndef GPS_EN_ACTIVE
    #ifdef PIN_GPS_EN_ACTIVE
        #define GPS_EN_ACTIVE PIN_GPS_EN_ACTIVE
    #else
        #define GPS_EN_ACTIVE HIGH
    #endif
#endif

#ifndef GPS_RESET
    #ifdef PIN_GPS_RESET
        #define GPS_RESET PIN_GPS_RESET
    #else
        #define GPS_RESET (-1)
    #endif
#endif

#ifndef GPS_RESET_ACTIVE
    #ifdef PIN_GPS_RESET_ACTIVE
        #define GPS_RESET_ACTIVE PIN_GPS_RESET_ACTIVE
    #else
        #define GPS_RESET_ACTIVE LOW
    #endif
#endif

class MicroNMEALocationProvider : public LocationProvider {
    char _nmeaBuffer[100];
    MicroNMEA nmea;
    mesh::RTCClock* _clock;
    Stream* _gps_serial;
    RefCountedDigitalPin* _peripher_power;
    int8_t _claims = 0;
    int _pin_reset;
    int _pin_en;
    unsigned long next_check = 0;
    unsigned long _last_time_sync = 0;
    // The clock is set only from the time and date text of an RMC sentence
    // that passed its checksum, once consecutive sentences agree with
    // millis(). MicroNMEA's own date/time fields can still hold values from
    // an earlier sentence when the latest one is malformed.
    uint32_t _clock_sample = 0;      // UTC seconds from the last accepted RMC
    uint32_t _clock_sample_ms = 0;   // millis() when that second was first seen
    uint8_t _clock_samples = 0;      // consecutive samples that agreed
    static const uint8_t CLOCK_SAMPLES_REQUIRED = 3;
    static const uint32_t CLOCK_SAMPLE_MAX_AGE_MS = 2000;
    static const int32_t CLOCK_SAMPLE_TOLERANCE_S = 2;
    static const unsigned long TIME_SYNC_INTERVAL = 1800000; // Re-sync every 30 minutes

#ifdef GPS_L76K
    void configureL76K() {
        // Reapply the constellation mode at each provider start instead of
        // assuming it survives a receiver power cycle.
        // Mode 7 enables GPS + BeiDou + GLONASS.
        delay(250);
        MicroNMEA::sendSentence(*_gps_serial, "$PCAS04,7");
        delay(250);
    }
#endif
    // Observation-only diagnostics. These fields do not change GPS power,
    // timing, reset behaviour, or parser input.
    uint32_t _uart_bytes = 0;
    uint32_t _nmea_checksum_ok = 0;
    uint32_t _nmea_checksum_bad = 0;
    uint32_t _begin_calls = 0;
    uint32_t _stop_calls = 0;
    unsigned long _last_uart_ms = 0;
    unsigned long _last_nmea_ms = 0;
    unsigned long _last_fix_ms = 0;
    bool _uart_seen = false;
    bool _nmea_seen = false;
    bool _fix_seen = false;

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

    static bool isDigits(const char* s, int n) {
        for (int i = 0; i < n; i++) {
            if (s[i] < '0' || s[i] > '9') return false;
        }
        return true;
    }

    static uint8_t twoDigits(const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); }

    static uint8_t daysInMonth(uint16_t year, uint8_t month) {
        static const uint8_t days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        return month == 2 && leap ? 29 : days[month - 1];
    }

    // UTC seconds from the time and date fields of an RMC sentence, or 0 when
    // the sentence does not carry a complete time, an 'A' (valid) status and a
    // complete date. The checksum covers only what comes before the first
    // '*', and the date is the last field used, so it must end before it.
    static uint32_t rmcEpoch(const char* sentence) {
        const char* star = strchr(sentence, '*');
        const char* time = strchr(sentence, ',');
        if (star == NULL || time == NULL) return 0;
        time++;
        const char* time_end = strchr(time, ',');
        if (time_end == NULL || !isDigits(time, 6)) return 0;  // hhmmss
        if (time_end - time > 6                                 // optional .s+
            && (time[6] != '.' || time_end - time == 7 || !isDigits(time + 7, time_end - time - 7))) return 0;
        const char* status = time_end + 1;
        if (status[0] != 'A' || status[1] != ',') return 0;
        const char* date = status + 2;
        for (int field = 3; field <= 8; field++) {  // latitude .. course
            date = strchr(date, ',');
            if (date == NULL) return 0;
            date++;
        }
        if (date + 6 > star || !isDigits(date, 6) || (date[6] != ',' && date[6] != '*')) return 0;  // ddmmyy

        const uint8_t hour = twoDigits(time), minute = twoDigits(time + 2), second = twoDigits(time + 4);
        const uint8_t day = twoDigits(date), month = twoDigits(date + 2);
        const uint16_t year = 2000 + twoDigits(date + 4);
        if (hour > 23 || minute > 59 || second > 59 || month < 1 || month > 12
            || day < 1 || day > daysInMonth(year, month)) return 0;
        return DateTime(year, month, day, hour, minute, second).unixtime();
    }

    // Count consecutive RMC samples whose seconds advance with millis(). A
    // sentence without a usable time, or a repeat of the same second
    // (receivers faster than 1 Hz), is ignored. A sample that disagrees by
    // more than the tolerance (loop stalls, lost sentences) starts a new run.
    void noteClockSample(uint32_t epoch, uint32_t now) {
        if (epoch == 0) return;
        if (_clock_samples > 0) {
            const int32_t gps_s = (int32_t)(epoch - _clock_sample);
            const int32_t wall_s = (int32_t)((now - _clock_sample_ms) / 1000);
            const int32_t drift = gps_s - wall_s;
            if (gps_s >= 0 && drift >= -CLOCK_SAMPLE_TOLERANCE_S && drift <= CLOCK_SAMPLE_TOLERANCE_S) {
                if (gps_s == 0) return;
                if (_clock_samples < CLOCK_SAMPLES_REQUIRED) _clock_samples++;
                _clock_sample = epoch;
                _clock_sample_ms = now;
                return;
            }
        }
        _clock_samples = 1;
        _clock_sample = epoch;
        _clock_sample_ms = now;
    }

    bool clockSampleReady() const {
        return _clock_samples >= CLOCK_SAMPLES_REQUIRED
            && (uint32_t)(millis() - _clock_sample_ms) <= CLOCK_SAMPLE_MAX_AGE_MS;
    }

public :
    MicroNMEALocationProvider(Stream& ser, mesh::RTCClock* clock = NULL, int pin_reset = GPS_RESET, int pin_en = GPS_EN,RefCountedDigitalPin* peripher_power=NULL) :
    nmea(_nmeaBuffer, sizeof(_nmeaBuffer)), _clock(clock), _gps_serial(&ser), _peripher_power(peripher_power), _pin_reset(pin_reset), _pin_en(pin_en) {
        if (_pin_reset != -1) {
            pinMode(_pin_reset, OUTPUT);
            digitalWrite(_pin_reset, GPS_RESET_ACTIVE);
        }
        if (_pin_en != -1) {
            pinMode(_pin_en, OUTPUT);
            digitalWrite(_pin_en, !GPS_EN_ACTIVE);
        }
    }

    void claim() {
        _claims++;
        if (_peripher_power) _peripher_power->claim();
    }

    void release() {
        if (_claims == 0) return; // avoid negative _claims
        _claims--;
        if (_peripher_power) _peripher_power->release();
    }

    void begin() override {
        _begin_calls++;
        claim();
        if (_pin_en != -1) {
            digitalWrite(_pin_en, GPS_EN_ACTIVE);
        }
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, !GPS_RESET_ACTIVE);
        }
    }

    void reset() override {
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, GPS_RESET_ACTIVE);
            delay(10);
            digitalWrite(_pin_reset, !GPS_RESET_ACTIVE);
        }
#ifdef GPS_L76K
        // EnvironmentSensorManager calls reset() immediately after begin().
        // Configure only after that reset so the setting is not discarded.
        configureL76K();
#endif
    }

    void stop() override {
        _stop_calls++;
        if (_pin_en != -1) {
            digitalWrite(_pin_en, !GPS_EN_ACTIVE);
        }
        if (_pin_reset != -1) {
            digitalWrite(_pin_reset, GPS_RESET_ACTIVE);
        }
        release();
    }

    bool isEnabled() override {
        // directly read the enable pin if present as gps can be
        // activated/deactivated outside of here ...
        if (_pin_en != -1) {
            return digitalRead(_pin_en) == GPS_EN_ACTIVE;
        } else {
            return true; // no enable so must be active
        }
    }

    void syncTime() override {
        nmea.clear();
        _clock_samples = 0;
        LocationProvider::syncTime();
    }
    long getLatitude() override { return nmea.getLatitude(); }
    long getLongitude() override { return nmea.getLongitude(); }
    long getAltitude() override { 
        long alt = 0;
        nmea.getAltitude(alt);
        return alt;
    }
    long satellitesCount() override { return nmea.getNumSatellites(); }
    bool isValid() override { return nmea.isValid(); }

    long getTimestamp() override { 
        DateTime dt(nmea.getYear(), nmea.getMonth(),nmea.getDay(),nmea.getHour(),nmea.getMinute(),nmea.getSecond());
        return dt.unixtime();
    } 

    void drain() { while (_gps_serial->available()) _gps_serial->read(); }

    const char* getSentence() const { return nmea.getSentence(); }

    void sendSentence(const char *sentence) override {
        nmea.sendSentence(*_gps_serial, sentence);
    }

    bool waitFor(const char* prefix, uint32_t timeout_ms) {
        size_t plen = strlen(prefix);
        uint32_t timeout = millis() + timeout_ms;
        while ((int32_t)(millis() - timeout) < 0) {
            if (_gps_serial->available()) {
                char c = _gps_serial->read();
                #ifdef GPS_NMEA_DEBUG
                Serial.print(c);
                #endif
                if (nmea.process(c) && strncmp(nmea.getSentence(), prefix, plen) == 0) return true;
            } else {
                yield();
            }
        }
        return false;
    }

    void loop() override {

        while (_gps_serial->available()) {
            char c = _gps_serial->read();
            _uart_bytes++;
            _last_uart_ms = millis();
            _uart_seen = true;
            #ifdef GPS_NMEA_DEBUG
            Serial.print(c);
            #endif
            bool parsed = nmea.process(c);

            // MicroNMEA leaves the completed sentence in its buffer after the
            // first line terminator. CRLF therefore counts once: CR observes the
            // sentence, then LF clears the now-empty buffer.
            if ((c == '\0' || c == '\r' || c == '\n') && nmea.getSentence()[0] != '\0') {
                if (MicroNMEA::testChecksum(nmea.getSentence())) {
                    _nmea_checksum_ok++;
                    _last_nmea_ms = millis();
                    _nmea_seen = true;

                    // Refresh fix age only when a newly parsed GGA/RMC sentence
                    // carries a valid fix. Other valid NMEA sentences must not
                    // make an old position look fresh.
                    const char* message_id = nmea.getMessageID();
                    if (parsed && isValid() &&
                        (strcmp(message_id, "GGA") == 0 || strcmp(message_id, "RMC") == 0)) {
                        _last_fix_ms = millis();
                        _fix_seen = true;
                    }
                    if (strcmp(message_id, "RMC") == 0) {
                        noteClockSample(rmcEpoch(nmea.getSentence()), millis());
                    }
                } else {
                    _nmea_checksum_bad++;
                }
            }
        }

        if ((long)(millis() - next_check) > 0) {
            next_check = millis() + 1000;
            // Re-enable time sync periodically when GPS has valid fix
            if (!_time_sync_needed && _clock != NULL && (millis() - _last_time_sync) > TIME_SYNC_INTERVAL) {
                _time_sync_needed = true;
            }
            if (_time_sync_needed && clockSampleReady()) {
                if (_clock != NULL) {
                    _clock->setCurrentTime(_clock_sample);
                    _time_sync_needed = false;
                    _last_time_sync = millis();
                }
            }
        }
    }

    void formatDiagnostics(char* out, size_t out_size) override {
        if (out_size == 0) return;

        char uart_age[11];
        char nmea_age[11];
        char fix_age[11];
        formatAge(uart_age, sizeof(uart_age), _uart_seen, _last_uart_ms);
        formatAge(nmea_age, sizeof(nmea_age), _nmea_seen, _last_nmea_ms);
        formatAge(fix_age, sizeof(fix_age), _fix_seen, _last_fix_ms);

        // Compact enough for CommonCLI's 160-byte reply buffer, including the
        // "req:" prefix added by CommonCLI.
        snprintf(out, out_size,
            "en:%u ub:%lu ua:%s ok:%lu bad:%lu na:%s sat:%ld fix:%u fa:%s bc:%lu sc:%lu",
            isEnabled() ? 1U : 0U,
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
