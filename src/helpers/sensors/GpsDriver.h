#pragma once

#include <stdint.h>

class GpsDriver {
public:
    virtual ~GpsDriver() = default;

    virtual void begin() = 0;
    virtual void reset() = 0;
    virtual void stop() = 0;
    virtual bool isActive() const = 0;
    virtual bool hasHardwareEnableState() const { return false; }
    virtual bool isHardwareEnabled() const { return true; }
    // Returns true when the switch explicitly enables GPS in software.
    virtual bool pollGpsSwitch() { return false; }
    virtual bool detect(uint32_t timeout_ms) = 0;
};
