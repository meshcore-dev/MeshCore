#pragma once

#include <stdint.h>

class GpsDriver {
public:
    virtual ~GpsDriver() = default;

    virtual void begin() = 0;
    virtual void reset() = 0;
    virtual void stop() = 0;
    virtual bool isEnabled() const = 0;
    virtual bool hasHardwareEnableState() const { return false; }
    virtual bool isHardwareEnabled() const { return true; }
    virtual bool detect(uint32_t timeout_ms) = 0;
};
