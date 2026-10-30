#pragma once

#include "Mesh.h"
#include <stddef.h>
#include <stdio.h>


class LocationProvider {
public:
    // Optional veto on GPS time before it is written to the clock (e.g. anti-spoofing).
    typedef bool (*TimeGuardFn)(void* ctx, uint32_t gps_time, uint32_t clock_time);

protected:
    bool _time_sync_needed = true;
    TimeGuardFn _time_guard = NULL;
    void* _time_guard_ctx = NULL;

    bool timeSyncAllowed(uint32_t gps_time, uint32_t clock_time) {
        return _time_guard == NULL || _time_guard(_time_guard_ctx, gps_time, clock_time);
    }

public:
    void setTimeGuard(TimeGuardFn fn, void* ctx) { _time_guard = fn; _time_guard_ctx = ctx; }
    virtual uint8_t getHDOP() { return 255; }                    // in tenths, 255 = unknown
    virtual bool getClockOffset(long& offset) { return false; }  // GPS time minus clock time
    virtual bool applyTimeNow() { return false; }                // write GPS time to the clock, bypassing the guard
    virtual void syncTime() { _time_sync_needed = true; }
    virtual bool waitingTimeSync() { return _time_sync_needed; }
    virtual long getLatitude() = 0;
    virtual long getLongitude() = 0;
    virtual long getAltitude() = 0;
    virtual long satellitesCount() = 0;
    virtual bool isValid() = 0;
    virtual long getTimestamp() = 0;
    virtual void sendSentence(const char * sentence);
    virtual bool waitFor(const char* prefix, uint32_t timeout_ms) { return false; }
    virtual void drain() { }
    virtual void reset() = 0;
    virtual void begin() = 0;
    virtual void stop() = 0;
    virtual void loop() = 0;
    virtual bool isEnabled() = 0;

    // Format compact diagnostics in the caller-provided buffer. Providers that
    // have more information (for example UART counters) can override this.
    virtual void formatDiagnostics(char* out, size_t out_size) {
        if (out_size == 0) return;
        snprintf(out, out_size, "en:%u sat:%ld fix:%u",
            isEnabled() ? 1U : 0U,
            satellitesCount(),
            isValid() ? 1U : 0U);
    }
};
