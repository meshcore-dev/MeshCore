#pragma once

#include <stdint.h>
#include <string.h>

// Read the bootloader's OTA capability marker so the app can decide, BEFORE staging+approving+rebooting,
// whether THIS device's bootloader can actually apply a `.mota`. Without this the app would reboot into a
// bootloader that silently can't apply (stock Adafruit, or an OLDER OTAFIX predating a `.mota` format
// change) and the device would just come back up unchanged.
//
// Mirror of Adafruit_nRF52_Bootloader_OTAFIX/src/ota_bl_info.h — keep byte-identical.
// nRF52 only (the bootloader flash is memory-mapped + readable by the app); a no-op elsewhere.

#if defined(NRF52_PLATFORM)
  #include "OtaFlashLayout_nrf52.h"
#endif

namespace mesh {
namespace ota {

// 16-byte marker: magic[8] "MOTABLDR" + apply_abi(2) + codec_mask(2) + reserved(4).
static const uint8_t OTA_BL_MAGIC[8] = { 'M','O','T','A','B','L','D','R' };

struct OtaBlCaps {
  bool     present = false;
  uint16_t apply_abi = 0;    // max .mota format_ver the bootloader can apply
  uint16_t codec_mask = 0;   // bit i set => can apply codec_id i (in-place delta = bit 2)
};

// Scan the bootloader flash region for the marker. Returns {present=false} if not found / non-nRF52.
inline OtaBlCaps ota_bootloader_caps() {
  OtaBlCaps c;
#if defined(NRF52_PLATFORM)
  const uint8_t* lo = (const uint8_t*)(uintptr_t)MOTA_NRF52_BL_START;
  const uint8_t* hi = (const uint8_t*)(uintptr_t)MOTA_NRF52_BL_END;
  for (const uint8_t* p = lo; p + 16 <= hi; p++) {
    if (p[0] != OTA_BL_MAGIC[0] || memcmp(p, OTA_BL_MAGIC, 8) != 0) continue;
    c.present    = true;
    c.apply_abi  = (uint16_t)(p[8]  | ((uint16_t)p[9]  << 8));
    c.codec_mask = (uint16_t)(p[10] | ((uint16_t)p[11] << 8));
    break;
  }
#endif
  return c;
}

// True if this device's bootloader can apply a .mota of the given format_ver + codec_id.
inline bool ota_bootloader_can_apply(uint8_t format_ver, uint8_t codec_id) {
  OtaBlCaps c = ota_bootloader_caps();
  return c.present && c.apply_abi >= format_ver && (c.codec_mask & (1u << codec_id)) != 0;
}

} // namespace ota
} // namespace mesh
