#pragma once

#include <stdint.h>

/**
 * @brief The settings a bridge implementation needs, kept in their own type.
 *
 * A bridge must not depend on *which* NodePrefs it is handed. MeshCore has two
 * unrelated ones - the CLI class the repeater examples build against, and the
 * companion's - so a bridge that takes `NodePrefs*` can only ever be compiled
 * into the first: including it from a companion redefines the class outright.
 *
 * Both NodePrefs classes inherit this, so `_prefs.bridge_enabled` and the other
 * existing call sites keep working with no change.
 */
struct BridgePrefs {
  uint8_t bridge_enabled = 0;    // boolean
  uint16_t bridge_delay = 0;     // milliseconds (default 500 ms)
  uint8_t bridge_pkt_src = 0;    // 0 = logTx, 1 = logRx (default logTx)
  uint32_t bridge_baud = 0;      // 9600, 19200, 38400, 57600, 115200 (default 115200)
  uint8_t bridge_channel = 0;    // 1-14 (ESP-NOW only)
  char bridge_secret[16] = {0};  // XOR key for bridge packets (ESP-NOW only)
};
