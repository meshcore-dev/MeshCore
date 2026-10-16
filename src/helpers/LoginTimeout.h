#pragma once

#include <stdint.h>

namespace mesh {

// Direct logins to repeaters receive a flood-routed response because the login
// request does not carry a return path. Both timeout estimates cover a complete
// exchange, so use the larger estimate rather than adding them together.
inline uint32_t selectDirectLoginTimeout(bool reply_uses_flood, uint32_t direct_timeout,
                                         uint32_t flood_timeout) {
  if (reply_uses_flood && flood_timeout > direct_timeout) return flood_timeout;
  return direct_timeout;
}

}  // namespace mesh
