#pragma once

#include "Mesh.h"
#include <helpers/RefCountedDigitalPin.h>

class LocationProvider {
protected:
    mesh::RTCClock* _clock;
    RefCountedDigitalPin* _peripher_power;
    bool _time_sync_needed = true;
    bool _powered = false;
    bool _active = false;
    long _next_check = 0;
    long _time_valid = 0;

    void updateTimeSync() {
        if (!isValid()) _time_valid = 0;
        if (millis() > _next_check) {
            _next_check = millis() + 1000;
            if (_time_sync_needed && _time_valid > 2) {
                if (_clock != NULL) {
                    _clock->setCurrentTime(getTimestamp());
                    _time_sync_needed = false;
                }
            }
            if (isValid()) {
                _time_valid++;
            }
        }
    }

    // Override to read location data from hardware (serial, I2C, etc.)
    virtual void pollLocation() = 0;

public:
    LocationProvider(mesh::RTCClock* clock = NULL, RefCountedDigitalPin* peripher_power = NULL)
      : _clock(clock), _peripher_power(peripher_power) {
    }

    void setClock(mesh::RTCClock* clock) { _clock = clock; }

    void powerOn() {
        if (!_peripher_power || _powered) return;
        MESH_DEBUG_PRINTLN("GPS: power on");
        _peripher_power->claim();
        _powered = true;
    }

    void powerOff() {
        if (!_peripher_power || !_powered) return;
        MESH_DEBUG_PRINTLN("GPS: power off");
        _peripher_power->release();
        _powered = false;
    }

    virtual void reset() { }

    virtual bool isEnabled() { return _powered; }

    virtual void syncTime() { _time_sync_needed = true; }
    virtual bool waitingTimeSync() { return _time_sync_needed; }
    virtual long getLatitude() = 0;
    virtual long getLongitude() = 0;
    virtual long getAltitude() = 0;
    virtual long satellitesCount() = 0;
    virtual bool isValid() = 0;
    virtual long getTimestamp() = 0;
    virtual bool detect() { return false; }
    virtual void begin() = 0;
    virtual void stop() = 0;

    void loop() {
        if (!_active) return;
        pollLocation();
        updateTimeSync();
    }
};
