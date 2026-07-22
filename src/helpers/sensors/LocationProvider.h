#pragma once

#include "Mesh.h"


class LocationProvider {
protected:
    bool _time_sync_needed = true;

    bool powersaving_enabled = false;
    unsigned long _wake_duration_secs = 86400; // Full day
    unsigned long _sleep_duration_secs = 0; // No off
    unsigned long _next_wake = 0;
    unsigned long _next_sleep = 0;
    unsigned long _last_valid_time_sync = 0;

public:
    virtual void syncTime() { _time_sync_needed = true; }
    virtual bool waitingTimeSync() { return _time_sync_needed; }
    virtual void stopTimeSync() { _time_sync_needed = false; }

    virtual void setPowerSavingProfile(unsigned long wake_duration_secs, unsigned long sleep_duration_secs) {
      _wake_duration_secs = wake_duration_secs;
      _sleep_duration_secs = sleep_duration_secs;
    }

    virtual void enablePowerSaving(bool enabled) { powersaving_enabled = enabled; _next_wake = 0; _next_sleep = 0; }
    virtual bool isPowerSavingEnabled() { return powersaving_enabled; }
    virtual void setNextWake() { _next_wake = millis() + _sleep_duration_secs * 1000UL; }
    virtual unsigned long getNextWake() { return _next_wake; }
    virtual void setNextSleep() { _next_sleep = millis() + _wake_duration_secs * 1000UL; }
    virtual unsigned long getNextSleep() { return _next_sleep; }
    virtual unsigned long getLastValidTimeSync() { return _last_valid_time_sync; }
    
    virtual long getLatitude() = 0;
    virtual long getLongitude() = 0;
    virtual long getAltitude() = 0;
    virtual long satellitesCount() = 0;
    virtual bool isValid() = 0;
    virtual long getTimestamp() = 0;
    virtual void sendSentence(const char * sentence);
    virtual void reset() = 0;
    virtual void begin() = 0;
    virtual void stop() = 0;
    virtual void loop() = 0;
    virtual bool isEnabled() = 0;
    virtual void setPinEn(int pin_en) = 0;
    virtual int getPinEn() = 0;
};
