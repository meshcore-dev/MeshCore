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

} // namespace ota
} // namespace mesh
