#pragma once

#include "Mesh.h"
#include <stddef.h>
#include <stdio.h>


class GpsGuard;

class LocationProvider {
public:
    // Last valid fix, always filled in one go so readers get a consistent snapshot.
    struct LastFix {
        bool valid = false;
        long latitude = 0;
        long longitude = 0;
        long altitude = 0;
        long satellites = 0;
        long timestamp = 0;
        unsigned long acquiredAt = 0;   // millis() when the fix was stored
    };

    struct FixStats {
        uint32_t lastTtffMs = 0; //time to first fix in ms
        uint32_t maxTtffMs = 0;
        uint32_t consecutiveFixes = 0;
        uint32_t consecutiveMisses = 0;
    };

protected:
    bool _time_sync_needed = true;
    bool _gpsOn = false;
    LastFix _lastFix;
    FixStats _fixStats;

    void fixStatsFix(uint32_t ttffMs) {
        _fixStats.lastTtffMs = ttffMs;
        if (ttffMs > _fixStats.maxTtffMs) _fixStats.maxTtffMs = ttffMs;
        _fixStats.consecutiveFixes++;
        _fixStats.consecutiveMisses = 0;
    }
    void fixStatsMiss() {
        _fixStats.consecutiveMisses++;
        _fixStats.consecutiveFixes = 0;
    }

public:
    virtual ~LocationProvider() = default;

    virtual void syncTime() { _time_sync_needed = true; }
    virtual bool waitingTimeSync() { return _time_sync_needed; }
    virtual long getLatitude() = 0;
    virtual long getLongitude() = 0;
    virtual long getAltitude() = 0;
    virtual long satellitesCount() = 0;
    virtual bool isValid() = 0;                       // current fix is valid
    virtual bool hasValidFix() { return getLastFix().valid || isValid(); }  // a valid fix was obtained at some point
    virtual LastFix getLastFix() const { return _lastFix; }  // snapshot of the last valid fix
    virtual FixStats getFixStats() const { return _fixStats; }
    virtual long getTimestamp() = 0;
    virtual void sendSentence(const char * sentence);
    virtual bool waitFor(const char* prefix, uint32_t timeout_ms) { return false; }
    virtual void drain() { }
    virtual bool detect(uint32_t timeout_ms) { return false; }
    virtual bool keepEnabledAfterDetection() const { return false; }
    // Power on, detect, then leave the GPS on or off as the board wants.
    // Returns true if a GPS is present.
    virtual bool autoDetect() {
        setGpsOn(true);
    #ifdef ENV_SKIP_GPS_DETECT
        bool found = true;
    #else
        bool found = detect(5000);
    #endif
    #ifdef PERSISTANT_GPS
        if (found) return true;
    #endif
        if (found && keepEnabledAfterDetection()) return true;
        setGpsOn(false);
        return found;
    }
    virtual void setGpsPeriod(uint32_t seconds) { }
    virtual uint32_t getGpsPeriod() const { return 0; }
    virtual uint32_t getLastFixAge() const { // in seconds, UINT32_MAX if never
        const LastFix f = getLastFix();
        return f.valid ? (uint32_t)(millis() - f.acquiredAt) / 1000UL : UINT32_MAX;
    }
    virtual bool hasHardwareEnableState() const { return false; }
    virtual bool isHardwareEnabled() const { return true; }
    virtual bool isGpsOn() const { return _gpsOn; }
    virtual uint32_t getAcquisitionAge() const { return 0; }
    virtual bool startOnce() { return false; }
    virtual void setGpsOn(bool on) {
        _gpsOn = on;
        if (on) {
            begin();
            reset();
        } else {
            stop();
        }
    }
    virtual void reset() = 0;
    virtual void begin() = 0;
    virtual void stop() = 0;
    virtual void loop() = 0;
    virtual bool isActive() = 0;

    // Format compact diagnostics in the caller-provided buffer. Providers that
    // have more information (for example UART counters) can override this.
    // Anti-spoofing guard (GPS_GUARD), nullptr if this provider has none
    virtual GpsGuard* getGuard() { return nullptr; }
    virtual bool getClockOffset(long& offset) { return false; }  // GPS time minus clock time
    virtual bool applyTimeNow() { return false; }                // write GPS time to the clock now

    virtual void formatDiagnostics(char* out, size_t out_size) {
        if (out_size == 0) return;
        snprintf(out, out_size, "en:%u sat:%ld fix:%u",
            isActive() ? 1U : 0U,
            satellitesCount(),
            isValid() ? 1U : 0U);
    }
};
