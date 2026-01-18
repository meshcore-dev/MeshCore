#ifdef NRF52_PLATFORM

#include "Watchdog.h"
#include <nrf.h>

// WDT reload request magic value
#define WDT_RR_VALUE 0x6E524635UL

// 30 second timeout at 32.768kHz clock
// CRV = timeout_seconds * 32768
#define WDT_TIMEOUT_SECONDS 30
#define WDT_CRV_VALUE (WDT_TIMEOUT_SECONDS * 32768UL)

namespace nrf52 {

// Init watchdog timer - return true if success, false if already running
bool Watchdog::begin() {
    // Check if already running - WDT cannot be reconfigured once started
    if (NRF_WDT->RUNSTATUS) {
        return false;
    }
    // Configure WDT to pause during sleep and halt modes
    NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Pause << WDT_CONFIG_SLEEP_Pos) |
                      (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);

    // Set timeout value (30 seconds)
    NRF_WDT->CRV = WDT_CRV_VALUE;

    // Enable reload request register 0
    NRF_WDT->RREN = WDT_RREN_RR0_Enabled << WDT_RREN_RR0_Pos;

    // Start the watchdog
    NRF_WDT->TASKS_START = 1;

    return true;
}

// Feed watchdog timer within timout interval to prevent board reset
void Watchdog::feed() {
    // Write magic value to reload request register 0
    NRF_WDT->RR[0] = WDT_RR_VALUE;
}

bool Watchdog::isEnabled() {
    return NRF_WDT->RUNSTATUS != 0;
}

} // namespace nrf52

#endif // NRF52_PLATFORM
