#pragma once
#include <stdint.h>

struct TrackerPrefs {
    char     callsign[17];      // null-terminated, max 16 visible chars
    uint32_t interval_secs;     // broadcast interval (10-300)
    uint8_t  role;              // KESTREL_ROLE_*
    uint8_t  channel_idx;       // which channel to broadcast on
};
