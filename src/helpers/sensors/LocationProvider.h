#pragma once

#include "Mesh.h"


class LocationProvider {
protected:
    bool _time_sync_needed = true;

public:
    virtual void syncTime() { _time_sync_needed = true; }
    virtual bool waitingTimeSync() { return _time_sync_needed; }
    virtual long getLatitude() = 0;
    virtual long getLongitude() = 0;
    virtual long getAltitude() = 0;
    virtual long satellitesCount() = 0;
    virtual bool isValid() = 0;
    // UTC seconds since the epoch, or 0 when no valid time is available.
    // Callers must treat 0 as "no time" rather than as a timestamp.
    // Unsigned to match mesh::RTCClock, and so times past 2038 stay
    // representable on targets where long is 32 bit signed.
    virtual uint32_t getTimestamp() = 0;
    virtual void sendSentence(const char * sentence);
    virtual void reset() = 0;
    virtual void begin() = 0;
    virtual void stop() = 0;
    virtual void loop() = 0;
    virtual bool isEnabled() = 0;
};
