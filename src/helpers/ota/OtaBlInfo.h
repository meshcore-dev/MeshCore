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

// 16-byte marker: magic[8] "MOTABLDR" + apply_abi(2) + codec_mask(2) + features(1) + reserved(3).
static const uint8_t OTA_BL_MAGIC[8] = { 'M','O','T','A','B','L','D','R' };

#define MOTA_BL_FEAT_WDT_FEED  0x01u   // bootloader feeds app WDT during mota apply / DFU / UF2

struct OtaBlCaps {
  bool     present = false;
  uint16_t apply_abi = 0;    // max .mota format_ver the bootloader can apply
  uint16_t codec_mask = 0;   // bit i set => can apply codec_id i (in-place delta = bit 2)
  uint8_t  features = 0;     // MOTA_BL_FEAT_*
};

// Scan the bootloader flash region for the marker. Returns {present=false} if not found / non-nRF52.
inline OtaBlCaps ota_bootloader_caps() {
  OtaBlCaps c;
#if defined(NRF52_PLATFORM)
#if defined(OTA_BL_CAPS_TEST_REGION)
  const uint8_t* lo = OTA_BL_CAPS_TEST_REGION;
  const uint8_t* hi = lo + OTA_BL_CAPS_TEST_REGION_SIZE;
#else
  const uint8_t* lo = (const uint8_t*)(uintptr_t)MOTA_NRF52_BL_START;
  const uint8_t* hi = (const uint8_t*)(uintptr_t)MOTA_NRF52_BL_END;
#endif
  for (const uint8_t* p = lo; p + 16 <= hi; p++) {
    if (p[0] != OTA_BL_MAGIC[0] || memcmp(p, OTA_BL_MAGIC, 8) != 0) continue;
    c.present    = true;
    c.apply_abi  = (uint16_t)(p[8]  | ((uint16_t)p[9]  << 8));
    c.codec_mask = (uint16_t)(p[10] | ((uint16_t)p[11] << 8));
    c.features   = p[12];
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

// True if this bootloader feeds a WDT left running across soft reset into mota apply / DFU / UF2.
inline bool ota_bootloader_wdt_feed() {
  OtaBlCaps c = ota_bootloader_caps();
  return c.present && (c.features & MOTA_BL_FEAT_WDT_FEED) != 0;
}

// True when app WDT must stay off: mota-apply bootloader advertised but lacks WDT feed during apply.
// Stock / non-mota bootloaders (no MOTABLDR marker) are not blocked.
inline bool ota_bootloader_blocks_wdt() {
  OtaBlCaps c = ota_bootloader_caps();
  return c.present && (c.features & MOTA_BL_FEAT_WDT_FEED) == 0;
}

} // namespace ota
} // namespace mesh
