#pragma once

#include "Mesh.h"


class GpsDriver {
public:
    virtual bool init() = 0;

    // Hardware control 
    virtual void begin() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;
    virtual bool isEnabled() = 0;

    // Data synchronization
    virtual void sync() = 0;

    // Data access
    virtual long getLatitude() = 0;
    virtual long getLongitude() = 0;
    virtual long getAltitude() = 0;
    virtual long satellitesCount() = 0;
    virtual long getTimestamp() = 0;

    // Optional command interface
    virtual void sendSentence(const char * sentence);
};
