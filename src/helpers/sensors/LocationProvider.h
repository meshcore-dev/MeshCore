#pragma once

#include "Mesh.h"


class LocationProvider {
protected:
    mesh::RTCClock* _clock;
    bool _time_sync_needed = true;
    unsigned long _last_time_sync = 0;
    long _time_valid = 0;

    void _syncTimeIfNeeded() {
        if (isValid()) {
            _time_valid ++;
        } else {
            _time_valid = 0;
        }

        if (_time_sync_needed && _time_valid > 3) {
            if (_clock != NULL) {
                _clock->setCurrentTime(getTimestamp());
                MESH_DEBUG_PRINTLN("Synced time from GPS: %u", _clock->getCurrentTime());
                _time_sync_needed = false;
                _last_time_sync = millis();
            }
        }
    };

public:
    LocationProvider(mesh::RTCClock* clock = NULL) :
    _clock(clock) {}
    virtual void syncTime() { _time_sync_needed = true; }
    virtual bool waitingTimeSync() { return _time_sync_needed; }
    virtual long getLatitude() = 0;
    virtual long getLongitude() = 0;
    virtual long getAltitude() = 0;
    virtual long satellitesCount() = 0;
    virtual bool isValid() = 0;
    virtual long getTimestamp() = 0;
    virtual void sendSentence(const char * sentence);
    virtual void reset() = 0;
    virtual void configure() = 0;
    virtual void begin() = 0;
    virtual void stop() = 0;
    virtual void loop() = 0;
    virtual bool isEnabled() = 0;
};
