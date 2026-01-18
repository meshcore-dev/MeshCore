#pragma once

#ifdef NRF52_PLATFORM

#include <stdint.h>

namespace nrf52 {

class Watchdog {
public:
    static bool begin();
    static void feed();
    static bool isEnabled();
};

} // namespace nrf52

#endif // NRF52_PLATFORM
