#include "helpers/asr650x/FlashRecord.h"

#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>

static asr650x::PersistState sample() {
  asr650x::PersistState s;
  memset(&s, 0, sizeof(s));
  s.has_identity = true;
  for (int i = 0; i < 64; i++)
    s.prv[i] = (uint8_t)(i + 1);
  for (int i = 0; i < 32; i++)
    s.pub[i] = (uint8_t)(0x80 + i);
  s.has_prefs = true;
  strcpy(s.node_name, "node-test");
  s.freq = 921.5f;
  s.bw = 62.5f;
  s.sf = 7;
  s.cr = 5;
  s.tx_power_dbm = 14;
  return s;
}

TEST(Asr650xFlashRecord, Behaves) {
  uint8_t buf[asr650x::PERSIST_SIZE];
  EXPECT_TRUE(asr650x::crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u); // standard CRC-32 check value

  asr650x::PersistState a = sample(), b;
  asr650x::persistEncode(a, buf);
  EXPECT_TRUE(asr650x::persistDecode(buf, b));
  EXPECT_TRUE(b.has_identity && memcmp(b.prv, a.prv, 64) == 0 && memcmp(b.pub, a.pub, 32) == 0);
  EXPECT_TRUE(b.has_prefs && strcmp(b.node_name, "node-test") == 0);
  EXPECT_TRUE(b.freq == 921.5f && b.bw == 62.5f && b.sf == 7 && b.cr == 5 && b.tx_power_dbm == 14);

  /* identity only / prefs only */
  a.has_prefs = false;
  asr650x::persistEncode(a, buf);
  EXPECT_TRUE(asr650x::persistDecode(buf, b) && b.has_identity && !b.has_prefs);
  a = sample();
  a.has_identity = false;
  asr650x::persistEncode(a, buf);
  EXPECT_TRUE(asr650x::persistDecode(buf, b) && !b.has_identity && b.has_prefs);

  /* every single-byte corruption must be rejected and reset the state to empty */
  a = sample();
  asr650x::persistEncode(a, buf);
  for (size_t i = 0; i < asr650x::PERSIST_SIZE; i++) {
    uint8_t bad[asr650x::PERSIST_SIZE];
    memcpy(bad, buf, sizeof(bad));
    bad[i] ^= 0x01;
    asr650x::PersistState c = sample();
    EXPECT_TRUE(!asr650x::persistDecode(bad, c));
    EXPECT_TRUE(!c.has_identity && !c.has_prefs);
  }
  /* erased flash (all 0x00 / all 0xFF) is not a valid record */
  uint8_t z[asr650x::PERSIST_SIZE];
  memset(z, 0x00, sizeof(z));
  EXPECT_TRUE(!asr650x::persistDecode(z, b));
  memset(z, 0xFF, sizeof(z));
  EXPECT_TRUE(!asr650x::persistDecode(z, b));
  /* a name without NUL terminator in the record is terminated on decode */
  a = sample();
  memset(a.node_name, 'A', 32);
  asr650x::persistEncode(a, buf);
  EXPECT_TRUE(asr650x::persistDecode(buf, b) && b.node_name[31] == 0);
  /* GPS settings round-trip and are covered by the CRC */
  {
    asr650x::PersistState g = sample();
    g.gps_off = true;
    g.gps_interval = 3600;
    asr650x::persistEncode(g, buf);
    EXPECT_TRUE(buf[147] == 1);
    EXPECT_TRUE(buf[152] == (3600 & 0xFF) && buf[153] == ((3600 >> 8) & 0xFF) && buf[154] == 0 &&
                buf[155] == 0);
    asr650x::PersistState h;
    EXPECT_TRUE(asr650x::persistDecode(buf, h) && h.gps_off && h.gps_interval == 3600);
    buf[153] ^= 1;
    EXPECT_TRUE(!asr650x::persistDecode(buf, h)); // corrupting the interval byte is detected
  }
  /* a legacy record (bytes 147 and 152..155 are zero) reads as 'GPS on, interval 0' */
  {
    asr650x::PersistState legacy = sample();
    legacy.gps_off = false;
    legacy.gps_interval = 0;
    asr650x::persistEncode(legacy, buf);
    asr650x::PersistState r;
    EXPECT_TRUE(asr650x::persistDecode(buf, r) && !r.gps_off && r.gps_interval == 0);
  }
}

// version 2: the other companion settings the app can change (they used to be lost on reboot)
TEST(Asr650xFlashRecord, Version2KeepsCompanionSettings) {
  asr650x::PersistState a = sample(), b;
  a.has_ext = true;
  a.airtime_factor = 1.5f;
  a.rx_delay_base = 2.25f;
  a.tx_delay_factor = 0.5f;
  a.direct_tx_delay_factor = 0.25f;
  a.multi_acks = 1;
  a.manual_add_contacts = 1;
  a.telemetry_modes = 0x26;
  a.advert_loc_policy = 1;
  a.autoadd_config = 0x0E;
  a.autoadd_max_hops = 3;
  a.path_hash_mode = 2;
  a.tz_offset = -7;
  a.cad_enabled = 1;
  a.interference_threshold = 14;
  a.agc_reset_interval = 5;
  a.rx_boosted_gain = 1;
  uint8_t buf[asr650x::PERSIST_SIZE];
  asr650x::persistEncode(a, buf);
  ASSERT_TRUE(asr650x::persistDecode(buf, b));
  EXPECT_TRUE(b.has_ext);
  EXPECT_EQ(b.airtime_factor, 1.5f);
  EXPECT_EQ(b.rx_delay_base, 2.25f);
  EXPECT_EQ(b.tx_delay_factor, 0.5f);
  EXPECT_EQ(b.direct_tx_delay_factor, 0.25f);
  EXPECT_EQ(b.multi_acks, 1);
  EXPECT_EQ(b.manual_add_contacts, 1);
  EXPECT_EQ(b.telemetry_modes, 0x26);
  EXPECT_EQ(b.advert_loc_policy, 1);
  EXPECT_EQ(b.autoadd_config, 0x0E);
  EXPECT_EQ(b.autoadd_max_hops, 3);
  EXPECT_EQ(b.path_hash_mode, 2);
  EXPECT_EQ(b.tz_offset, -7);
  EXPECT_EQ(b.cad_enabled, 1);
  EXPECT_EQ(b.interference_threshold, 14);
  EXPECT_EQ(b.agc_reset_interval, 5);
  EXPECT_EQ(b.rx_boosted_gain, 1);
  EXPECT_EQ(b.freq, 921.5f); // version 1 fields unchanged
  buf[176] ^= 0x01;          // a corrupted settings byte is detected
  EXPECT_FALSE(asr650x::persistDecode(buf, b));
}

// a record written by the first firmware (version 1, 160 bytes, CRC at 156) still loads; the new settings are
// absent
TEST(Asr650xFlashRecord, Version1RecordStillLoads) {
  asr650x::PersistState a = sample();
  uint8_t v2[asr650x::PERSIST_SIZE];
  asr650x::persistEncode(a, v2);
  uint8_t v1[asr650x::PERSIST_SIZE];
  memset(v1, 0xFF, sizeof(v1)); // bytes after the old record: erased flash
  memcpy(v1, v2, 156);
  v1[4] = 1;
  uint32_t crc = asr650x::crc32(v1, 156);
  for (int i = 0; i < 4; i++)
    v1[156 + i] = (uint8_t)(crc >> (8 * i));
  asr650x::PersistState b;
  ASSERT_TRUE(asr650x::persistDecode(v1, b));
  EXPECT_FALSE(b.has_ext);
  EXPECT_TRUE(b.has_identity && b.has_prefs);
  EXPECT_EQ(memcmp(b.prv, a.prv, 64), 0);
  EXPECT_STREQ(b.node_name, "node-test");
}

// records are written with the "ASR1" magic; records from early builds ("KTM1") still load
TEST(Asr650xFlashRecord, MagicAndLegacyMagic) {
  asr650x::PersistState a = sample(), b;
  uint8_t buf[asr650x::PERSIST_SIZE];
  asr650x::persistEncode(a, buf);
  EXPECT_EQ(memcmp(buf, "ASR1", 4), 0);
  memcpy(buf, "KTM1", 4);
  uint32_t crc = asr650x::crc32(buf, 188);
  for (int i = 0; i < 4; i++)
    buf[188 + i] = (uint8_t)(crc >> (8 * i));
  ASSERT_TRUE(asr650x::persistDecode(buf, b));
  EXPECT_EQ(memcmp(b.prv, a.prv, 64), 0);
  memcpy(buf, "XXX1", 4);
  crc = asr650x::crc32(buf, 188);
  for (int i = 0; i < 4; i++)
    buf[188 + i] = (uint8_t)(crc >> (8 * i));
  EXPECT_FALSE(asr650x::persistDecode(buf, b));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
