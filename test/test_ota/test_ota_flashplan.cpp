#include <gtest/gtest.h>

#include "helpers/ota/OtaFlashLayout_nrf52.h"

using namespace mesh::ota;

// These lock down the nRF52 (RAK4631) single-slot staging geometry that OtaStoreFlashNrf52::begin()/
// reopen() rely on. A received `.mota` is placed bottom-aligned below the role's staging ceiling and
// above the running image. Companion builds use 0xD4000 (below ExtraFS); repeaters use 0xED000.

static constexpr uint32_t CEILING_COMPANION = 0x000D4000u;
static constexpr uint32_t CEILING_REPEATER  = 0x000ED000u;

TEST(OtaFlashPlan, StagesBelowFilesystemAndAboveApp) {
  uint32_t app_end = MOTA_NRF52_APP_BASE + 520u * 1024u;
  uint32_t start = 0xDEADBEEF;
  ASSERT_TRUE(mota_nrf52_stage_plan(64u * 1024u, app_end, CEILING_COMPANION, start));
  EXPECT_GE(start, app_end);
  EXPECT_LE(start + 64u * 1024u, CEILING_COMPANION);
  EXPECT_EQ(start % MOTA_NRF52_FLASH_PAGE, 0u);
}

TEST(OtaFlashPlan, BottomAlignedTrailerEndsAtCeiling) {
  uint32_t start = 0;
  uint32_t total = 100000;
  ASSERT_TRUE(mota_nrf52_stage_plan(total, MOTA_NRF52_APP_BASE, CEILING_COMPANION, start));
  EXPECT_EQ(start, (CEILING_COMPANION - total) & ~(MOTA_NRF52_FLASH_PAGE - 1));
  EXPECT_LE(start + total, CEILING_COMPANION);
  EXPECT_GT(start + total, CEILING_COMPANION - MOTA_NRF52_FLASH_PAGE);
}

TEST(OtaFlashPlan, RejectsOversizedContainer) {
  uint32_t start = 0;
  const uint32_t cap = CEILING_COMPANION - MOTA_NRF52_APP_BASE;
  ASSERT_TRUE(mota_nrf52_stage_plan(cap, MOTA_NRF52_APP_BASE, CEILING_COMPANION, start));
  EXPECT_EQ(start, MOTA_NRF52_APP_BASE);
  EXPECT_EQ(start + cap, CEILING_COMPANION);
  EXPECT_FALSE(mota_nrf52_stage_plan(cap + 1, MOTA_NRF52_APP_BASE, CEILING_COMPANION, start));
}

TEST(OtaFlashPlan, RejectsCollisionWithRunningImage) {
  uint32_t app_end = CEILING_COMPANION - 8u * 1024u;
  uint32_t start = 0;
  EXPECT_TRUE(mota_nrf52_stage_plan(4u * 1024u, app_end, CEILING_COMPANION, start));
  EXPECT_GE(start, app_end);
  EXPECT_FALSE(mota_nrf52_stage_plan(16u * 1024u, app_end, CEILING_COMPANION, start));
}

TEST(OtaFlashPlan, RejectsUndersizedContainer) {
  uint32_t start = 0;
  EXPECT_FALSE(mota_nrf52_stage_plan(12, MOTA_NRF52_APP_BASE, CEILING_COMPANION, start));
  EXPECT_TRUE(mota_nrf52_stage_plan(13, MOTA_NRF52_APP_BASE, CEILING_COMPANION, start));
}

TEST(OtaFlashPlan, CompanionCeilingBelowExtraFS) {
  EXPECT_EQ(CEILING_COMPANION, MOTA_NRF52_EXTRAFS_START);
  uint32_t start = 0;
  const uint32_t cap = CEILING_COMPANION - MOTA_NRF52_APP_BASE;
  ASSERT_TRUE(mota_nrf52_stage_plan(cap, MOTA_NRF52_APP_BASE, CEILING_COMPANION, start));
  EXPECT_LE(start + cap, MOTA_NRF52_EXTRAFS_START);
}

TEST(OtaFlashPlan, RepeaterCeilingReclaimsExtraFS) {
  const uint32_t cap = CEILING_REPEATER - MOTA_NRF52_APP_BASE;
  EXPECT_GT(cap, CEILING_COMPANION - MOTA_NRF52_APP_BASE);
  uint32_t start = 0;
  ASSERT_TRUE(mota_nrf52_stage_plan(cap, MOTA_NRF52_APP_BASE, CEILING_REPEATER, start));
  EXPECT_EQ(start, MOTA_NRF52_APP_BASE);
  EXPECT_LE(start + cap, MOTA_NRF52_INTERNALFS_START);
}

TEST(OtaFlashPlan, LeavesOutputUntouchedOnReject) {
  uint32_t start = 0x1234ABCD;
  const uint32_t cap = CEILING_COMPANION - MOTA_NRF52_APP_BASE;
  EXPECT_FALSE(mota_nrf52_stage_plan(cap + 1, MOTA_NRF52_APP_BASE, CEILING_COMPANION, start));
  EXPECT_EQ(start, 0x1234ABCDu);
}
