#pragma once

// Shared OTA flash-layout constants for the nRF52840 (RAK4631) single-slot delta-apply path.
// SINGLE SOURCE OF TRUTH — keep byte-identical with the bootloader's src/ota_layout.h.
//
// The running app occupies [APP_BASE, app_end]; the primary LittleFS (InternalFS) starts at FS_START.
// MeshCore stages a verified+approved `.mota` in the free flash below FS_START (bottom-aligned), then
// sets GPREGRET_OTA_APPLY and resets; the bootloader scans [APP_BASE, FS_START) for it and applies it
// in place. These must match the bootloader and the running SoftDevice's app base.

#include <stdint.h>

namespace mesh {
namespace ota {

static const uint32_t MOTA_NRF52_APP_BASE   = 0x00026000u;  // S140 end (== CODE_REGION_1_START)
// Staging ceiling: the lowest filesystem region above the app. RAK4631 companion builds use the
// extrafs ldscript with ExtraFS at 0xD4000..0xED000 (and InternalFS at 0xED000), while the repeater
// uses the default ldscript (InternalFS at 0xED000, 0xD4000..0xED000 free). 0xD4000 is the safe
// universal ceiling for ALL RAK4631 roles: staging below it never touches ExtraFS or InternalFS, and
// the app (~520 KB) sits well below 0xD4000 either way.
static const uint32_t MOTA_NRF52_FS_START   = 0x000D4000u;  // ExtraFS start (universal staging ceiling)
static const uint32_t MOTA_NRF52_FLASH_PAGE = 4096u;
static const uint8_t  GPREGRET_OTA_APPLY    = 0x6Au;        // distinct from DFU magics 0x57/0x4E/0xA8

// In-place patches are built with --inplace-memory = this (the apply workspace, from APP_BASE up).
// It must hold the new image (~520 KB) yet leave the staged mota room below FS_START: workspace ends
// at APP_BASE+this = 0xBE000, leaving 0xBE000..0xD4000 (~88 KB) for the staged delta. The bootloader
// also bounds writes to < the (scanned) mota start, so a mis-sized memory still fails safe.
static const uint32_t MOTA_NRF52_INPLACE_MEMORY = 0x00098000u;  // 608 KB (APP_BASE .. 0xBE000)

// Bootloader flash region (nRF52840: 39 KB ending just below the CF2/MBR-params pages). The app scans
// this for the bootloader capability marker (OtaBlInfo.h) to know whether THIS device's bootloader can
// actually apply a .mota before staging+approving+rebooting.
static const uint32_t MOTA_NRF52_BL_START = 0x000F4000u;
static const uint32_t MOTA_NRF52_BL_END   = 0x000FE000u;

// Compile-time layout-ordering invariants. If a constant above is edited inconsistently these fail the
// BUILD rather than silently letting a stage/apply corrupt the filesystem (user prefs) or the app.
static_assert((MOTA_NRF52_APP_BASE % MOTA_NRF52_FLASH_PAGE) == 0, "APP_BASE must be page-aligned");
static_assert((MOTA_NRF52_FS_START % MOTA_NRF52_FLASH_PAGE) == 0, "FS_START must be page-aligned");
static_assert(MOTA_NRF52_APP_BASE < MOTA_NRF52_FS_START,  "app must precede the staging ceiling");
static_assert(MOTA_NRF52_FS_START < MOTA_NRF52_BL_START,  "staging (+FS) must end below the bootloader");
static_assert(MOTA_NRF52_BL_START < MOTA_NRF52_BL_END,    "bootloader region must be non-empty");
// The in-place apply workspace [APP_BASE, APP_BASE+INPLACE_MEMORY) must end at/below the staging ceiling,
// so an in-place apply never writes into ExtraFS/InternalFS (where user prefs live).
static_assert(MOTA_NRF52_APP_BASE + MOTA_NRF52_INPLACE_MEMORY <= MOTA_NRF52_FS_START,
              "in-place apply workspace must end at or below the staging ceiling");

// Plan where to stage a received `.mota` of `total_size` bytes. It is placed bottom-aligned so its
// 5-byte trailer ends exactly at FS_START (the bootloader scans downward from there), and it must sit
// ENTIRELY within [app_end, FS_START): above the running image (`app_end` = APP_BASE + its EndF image_len)
// and below the filesystem region (ExtraFS/InternalFS — where user prefs live, assumed immutable).
// Returns false (and leaves out_start untouched) if it does not fit. This is the SINGLE place the FS
// ceiling + app-collision bounds are enforced; begin()/reopen() both go through it. Pure — no flash I/O —
// so it is unit-tested natively in test/test_ota/test_ota_flashplan.cpp.
inline bool mota_nrf52_stage_plan(uint32_t total_size, uint32_t app_end, uint32_t& out_start) {
  const uint32_t capacity = MOTA_NRF52_FS_START - MOTA_NRF52_APP_BASE;
  if (total_size < 13 || total_size > capacity) return false;   // 13 = header(8)+trailer(5); must fit below FS
  uint32_t start = (MOTA_NRF52_FS_START - total_size) & ~(MOTA_NRF52_FLASH_PAGE - 1);   // bottom-align down
  if (start < app_end) return false;                            // would overlap the running image
  out_start = start;
  return true;
}

} // namespace ota
} // namespace mesh
