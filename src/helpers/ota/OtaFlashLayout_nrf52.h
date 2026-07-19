#pragma once

// Shared OTA flash-layout constants for the nRF52840 (RAK4631) single-slot delta-apply path.
// SINGLE SOURCE OF TRUTH — keep byte-identical with the bootloader's src/ota_layout.h.
//
// The running app occupies [APP_BASE, app_end]; companion builds also use ExtraFS at 0xD4000 and
// InternalFS at 0xED000. MeshCore stages a verified+approved `.mota` in the free flash below the
// role's staging ceiling (bottom-aligned), then sets GPREGRET_OTA_APPLY and resets; the bootloader
// scans [APP_BASE, FS_START) for it and applies it in place.

#include <stdint.h>

namespace mesh {
namespace ota {

static const uint32_t MOTA_NRF52_APP_BASE        = 0x00026000u;  // S140 end (== CODE_REGION_1_START)
static const uint32_t MOTA_NRF52_EXTRAFS_START   = 0x000D4000u;  // companion ExtraFS (CustomLFS)
static const uint32_t MOTA_NRF52_INTERNALFS_START = 0x000ED000u; // primary LittleFS (/com_prefs)
static const uint32_t MOTA_NRF52_FLASH_PAGE      = 4096u;
static const uint8_t  GPREGRET_OTA_APPLY         = 0x6Au;        // distinct from DFU magics 0x57/0x4E/0xA8

// Per-role staging ceiling (build flag). Default 0xD4000 = below ExtraFS (companion-safe). Repeater/
// room-server envs set 0xED000 to reclaim the unused ExtraFS region on boards that never mount it.
#ifndef MOTA_STAGE_CEILING
#define MOTA_STAGE_CEILING 0x000D4000u
#endif
static const uint32_t MOTA_NRF52_STAGE_CEILING = MOTA_STAGE_CEILING;

// Bootloader scan ceiling — InternalFS start. The bootloader always scans up to here; companion
// firmware still stages below ExtraFS via MOTA_STAGE_CEILING.
static const uint32_t MOTA_NRF52_FS_START = MOTA_NRF52_INTERNALFS_START;

// Bootloader flash region (nRF52840: 39 KB ending just below the CF2/MBR-params pages). The app scans
// this for the bootloader capability marker (OtaBlInfo.h) to know whether THIS device's bootloader can
// actually apply a .mota before staging+approving+rebooting.
static const uint32_t MOTA_NRF52_BL_START = 0x000F4000u;
static const uint32_t MOTA_NRF52_BL_END   = 0x000FE000u;

static_assert((MOTA_NRF52_APP_BASE % MOTA_NRF52_FLASH_PAGE) == 0, "APP_BASE must be page-aligned");
static_assert((MOTA_NRF52_STAGE_CEILING % MOTA_NRF52_FLASH_PAGE) == 0, "STAGE_CEILING must be page-aligned");
static_assert((MOTA_NRF52_FS_START % MOTA_NRF52_FLASH_PAGE) == 0, "FS_START must be page-aligned");
static_assert(MOTA_NRF52_APP_BASE < MOTA_NRF52_STAGE_CEILING, "app must precede the staging ceiling");
static_assert(MOTA_NRF52_STAGE_CEILING <= MOTA_NRF52_FS_START, "staging ceiling must not enter InternalFS");
static_assert(MOTA_NRF52_FS_START < MOTA_NRF52_BL_START,  "staging (+FS) must end below the bootloader");
static_assert(MOTA_NRF52_BL_START < MOTA_NRF52_BL_END,    "bootloader region must be non-empty");

// Plan where to stage a received `.mota` of `total_size` bytes. It is placed bottom-aligned so its
// 5-byte trailer ends exactly at `fs_start` (the bootloader scans downward from there), and it must sit
// ENTIRELY within [app_end, fs_start): above the running image and below the role's staging ceiling.
// Returns false (and leaves out_start untouched) if it does not fit. Pure — no flash I/O — unit-tested
// in test/test_ota/test_ota_flashplan.cpp.
inline bool mota_nrf52_stage_plan(uint32_t total_size, uint32_t app_end, uint32_t fs_start,
                                  uint32_t& out_start) {
  const uint32_t capacity = fs_start - MOTA_NRF52_APP_BASE;
  if (total_size < 13 || total_size > capacity) return false;   // 13 = header(8)+trailer(5)
  uint32_t start = (fs_start - total_size) & ~(MOTA_NRF52_FLASH_PAGE - 1);
  if (start < app_end) return false;
  out_start = start;
  return true;
}

} // namespace ota
} // namespace mesh
