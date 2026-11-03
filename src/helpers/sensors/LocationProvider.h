#pragma once

#include "Mesh.h"
#include <stddef.h>
#include <stdio.h>


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

protected:
    bool _time_sync_needed = true;
    bool _gpsOn = false;
    LastFix _lastFix;

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
    virtual long getTimestamp() = 0;
    virtual void sendSentence(const char * sentence);
    virtual bool waitFor(const char* prefix, uint32_t timeout_ms) { return false; }
    virtual void drain() { }
    virtual bool detect(uint32_t timeout_ms) { return false; }
    virtual bool keepEnabledAfterDetection() const { return false; }
    virtual void setGpsPeriod(uint32_t seconds) { }
    virtual uint32_t getGpsPeriod() const { return 0; }
    virtual uint32_t getLastFixAge() const { // in seconds, UINT32_MAX if never
        const LastFix f = getLastFix();
        return f.valid ? (uint32_t)(millis() - f.acquiredAt) / 1000UL : UINT32_MAX;
    }
    virtual bool hasHardwareEnableState() const { return false; }
    virtual bool isHardwareEnabled() const { return true; }
    virtual bool isGpsOn() const { return _gpsOn; }
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
    virtual void formatDiagnostics(char* out, size_t out_size) {
        if (out_size == 0) return;
        snprintf(out, out_size, "en:%u sat:%ld fix:%u",
            isActive() ? 1U : 0U,
            satellitesCount(),
            isValid() ? 1U : 0U);
    }
};
