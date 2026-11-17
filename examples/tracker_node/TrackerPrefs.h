#pragma once
#include <stdint.h>

enum NodeMode : uint8_t {
    NODE_MODE_UNPROVISIONED = 0,  // first boot, no NVM config
    NODE_MODE_TRACKER       = 1,  // GPS PLI broadcaster
    NODE_MODE_RELAY         = 2   // mesh relay/repeater
};

struct TrackerPrefs {
    char     callsign[17];      // null-terminated, max 16 visible chars
    uint32_t interval_secs;     // broadcast interval (10-300)
    uint8_t  role;              // KESTREL_ROLE_*
    uint8_t  channel_idx;       // which channel to broadcast on
    NodeMode mode;              // boot mode: unprovisioned / tracker / relay
    uint8_t  squad_psk[32];     // 32-byte squad PSK (provisioned via NFC/BLE)
    uint8_t  _pad[2];           // alignment padding
};
