#include <gtest/gtest.h>

#include "helpers/ota/OtaFlashLayout_nrf52.h"

using namespace mesh::ota;

// These lock down the nRF52 (RAK4631) single-slot staging geometry that OtaStoreFlashNrf52::begin()/
// reopen() rely on. A received `.mota` is placed bottom-aligned below the filesystem region — ExtraFS
// (0xD4000) / InternalFS (0xED000), where the node's user preferences live — and above the running image.
// The prefs region is assumed IMMUTABLE (its bytes are outside the served/hashed self-image), so staging
// or an in-place apply must never reach into it. If a layout constant or the placement math is edited
// inconsistently, these fail here instead of silently corrupting prefs / the app on real hardware.

static constexpr uint32_t CAP = MOTA_NRF52_FS_START - MOTA_NRF52_APP_BASE;   // 0xAE000, 696 KB

// A typical running image (~520 KB) leaves room; the container lands strictly within (app_end, FS_START].
TEST(OtaFlashPlan, StagesBelowFilesystemAndAboveApp) {
  uint32_t app_end = MOTA_NRF52_APP_BASE + 520u * 1024u;
  uint32_t start = 0xDEADBEEF;
  ASSERT_TRUE(mota_nrf52_stage_plan(64u * 1024u, app_end, start));
  EXPECT_GE(start, app_end);                                    // never overlaps the running image
  EXPECT_LE(start + 64u * 1024u, MOTA_NRF52_FS_START);          // never reaches into ExtraFS/InternalFS/prefs
  EXPECT_EQ(start % MOTA_NRF52_FLASH_PAGE, 0u);                 // page-aligned (the flash erase unit)
}

// Bottom-aligned: the container's trailer ends AT FS_START (the bootloader scans downward from there),
// so start is the page-aligned FS_START - total_size and sits within one page of the ceiling.
TEST(OtaFlashPlan, BottomAlignedTrailerEndsAtCeiling) {
  uint32_t start = 0;
  uint32_t total = 100000;
  ASSERT_TRUE(mota_nrf52_stage_plan(total, MOTA_NRF52_APP_BASE, start));
  EXPECT_EQ(start, (MOTA_NRF52_FS_START - total) & ~(MOTA_NRF52_FLASH_PAGE - 1));
  EXPECT_LE(start + total, MOTA_NRF52_FS_START);
  EXPECT_GT(start + total, MOTA_NRF52_FS_START - MOTA_NRF52_FLASH_PAGE);   // within one page of the ceiling
}

// An exactly-capacity container (no app below it) fills the whole staging region; one byte more never fits.
TEST(OtaFlashPlan, RejectsOversizedContainer) {
  uint32_t start = 0;
  ASSERT_TRUE(mota_nrf52_stage_plan(CAP, MOTA_NRF52_APP_BASE, start));
  EXPECT_EQ(start, MOTA_NRF52_APP_BASE);                        // exactly fills [APP_BASE, FS_START)
  EXPECT_EQ(start + CAP, MOTA_NRF52_FS_START);
  EXPECT_FALSE(mota_nrf52_stage_plan(CAP + 1, MOTA_NRF52_APP_BASE, start));
}

// A container that would fit below FS_START on its own but overlaps the running image is refused —
// the pull fails cleanly rather than corrupting the app that is currently executing.
TEST(OtaFlashPlan, RejectsCollisionWithRunningImage) {
  uint32_t app_end = MOTA_NRF52_FS_START - 8u * 1024u;         // only 8 KB free below the ceiling
  uint32_t start = 0;
  EXPECT_TRUE(mota_nrf52_stage_plan(4u * 1024u, app_end, start));       // 4 KB fits in the 8 KB gap
  EXPECT_GE(start, app_end);
  EXPECT_FALSE(mota_nrf52_stage_plan(16u * 1024u, app_end, start));     // 16 KB does not
}

// Minimum container is header(8)+trailer(5)=13 bytes; anything smaller is not a container.
TEST(OtaFlashPlan, RejectsUndersizedContainer) {
  uint32_t start = 0;
  EXPECT_FALSE(mota_nrf52_stage_plan(12, MOTA_NRF52_APP_BASE, start));
  EXPECT_TRUE(mota_nrf52_stage_plan(13, MOTA_NRF52_APP_BASE, start));
}

// The user-preferences filesystems (ExtraFS @ 0xD4000, InternalFS @ 0xED000) are entirely ABOVE any
// staged container AND above the in-place apply workspace — the immutability the shared-firmware hashes
// depend on. Hard-code the FS addresses here (independent of the layout header) so a future edit to
// FS_START that drifts into a filesystem is caught.
TEST(OtaFlashPlan, PrefsRegionNeverStaged) {
  const uint32_t EXTRAFS_START    = 0xD4000u;   // companion ExtraFS (CustomLFS(0xD4000, 0x19000))
  const uint32_t INTERNALFS_START = 0xED000u;   // primary LittleFS (holds /com_prefs)
  EXPECT_LE(MOTA_NRF52_FS_START, EXTRAFS_START);      // staging ceiling at/below the first filesystem
  EXPECT_LT(EXTRAFS_START, INTERNALFS_START);
  // the largest possible staged container still ends at the ceiling, never into a filesystem
  uint32_t start = 0;
  ASSERT_TRUE(mota_nrf52_stage_plan(CAP, MOTA_NRF52_APP_BASE, start));
  EXPECT_LE(start + CAP, EXTRAFS_START);
  // and the in-place apply workspace ends below the filesystem too
  EXPECT_LE(MOTA_NRF52_APP_BASE + MOTA_NRF52_INPLACE_MEMORY, EXTRAFS_START);
}

// out_start is only written on success — a rejected plan must not clobber the caller's variable.
TEST(OtaFlashPlan, LeavesOutputUntouchedOnReject) {
  uint32_t start = 0x1234ABCD;
  EXPECT_FALSE(mota_nrf52_stage_plan(CAP + 1, MOTA_NRF52_APP_BASE, start));
  EXPECT_EQ(start, 0x1234ABCDu);
}
