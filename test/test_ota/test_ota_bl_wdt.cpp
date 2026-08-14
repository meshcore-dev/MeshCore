#include <gtest/gtest.h>
#include <cstring>

#define NRF52_PLATFORM 1
alignas(4096) static uint8_t g_bl_flash[4096];
#define OTA_BL_CAPS_TEST_REGION g_bl_flash
#define OTA_BL_CAPS_TEST_REGION_SIZE sizeof(g_bl_flash)

#include "helpers/ota/OtaBlInfo.h"

using namespace mesh::ota;

static void write_bl_marker(uint8_t features) {
  memset(g_bl_flash, 0xFF, sizeof(g_bl_flash));
  memcpy(g_bl_flash, OTA_BL_MAGIC, 8);
  g_bl_flash[8] = 2;   // apply_abi (MOTA_BL_APPLY_ABI)
  g_bl_flash[10] = (uint8_t)(1u << 2);   // in-place delta codec
  g_bl_flash[12] = features;
}

static uint8_t wdt_timeout_for_boot(bool wdt_enabled_pref, uint8_t pref_timeout_secs) {
  uint8_t wdt_to = 0;
  if (wdt_enabled_pref && !ota_bootloader_blocks_wdt()) {
    wdt_to = pref_timeout_secs < 1 ? 1 : (pref_timeout_secs > 255 ? 255 : pref_timeout_secs);
  }
  return wdt_to;
}

// EnvyBoot 0.1.x: mota_bl_info present but features == 0 (no WDT feed during apply).
TEST(OtaBlWdt, BlockedForMotaBootloaderWithoutWdtFeed) {
  write_bl_marker(0);
  OtaBlCaps c = ota_bootloader_caps();
  ASSERT_TRUE(c.present);
  EXPECT_EQ(c.features, 0u);
  EXPECT_FALSE(ota_bootloader_wdt_feed());
  EXPECT_TRUE(ota_bootloader_blocks_wdt());
  EXPECT_EQ(wdt_timeout_for_boot(true, 30), 0u);
}

TEST(OtaBlWdt, NotBlockedWhenBootloaderAdvertisesWdtFeed) {
  write_bl_marker(MOTA_BL_FEAT_WDT_FEED);
  EXPECT_TRUE(ota_bootloader_wdt_feed());
  EXPECT_FALSE(ota_bootloader_blocks_wdt());
  EXPECT_EQ(wdt_timeout_for_boot(true, 30), 30u);
}

// Stock Adafruit / OTAFIX without mota-apply marker: no in-place apply handoff, WDT OK.
TEST(OtaBlWdt, NotBlockedWhenMarkerMissing) {
  memset(g_bl_flash, 0xFF, sizeof(g_bl_flash));
  EXPECT_FALSE(ota_bootloader_caps().present);
  EXPECT_FALSE(ota_bootloader_wdt_feed());
  EXPECT_FALSE(ota_bootloader_blocks_wdt());
  EXPECT_EQ(wdt_timeout_for_boot(true, 30), 30u);
}
