#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include "helpers/asr650x/FlashRecord.h"

static asr650x::PersistState sample() {
  asr650x::PersistState s; memset(&s, 0, sizeof(s));
  s.has_identity = true;
  for (int i = 0; i < 64; i++) s.prv[i] = (uint8_t)(i + 1);
  for (int i = 0; i < 32; i++) s.pub[i] = (uint8_t)(0x80 + i);
  s.has_prefs = true;
  strcpy(s.node_name, "KTM-test");
  s.freq = 921.5f; s.bw = 62.5f; s.sf = 7; s.cr = 5; s.tx_power_dbm = 14;
  return s;
}

TEST(Asr650xFlashRecord, Behaves) {
  uint8_t buf[asr650x::PERSIST_SIZE];
  EXPECT_TRUE(asr650x::crc32((const uint8_t*)"123456789", 9) == 0xCBF43926u);   // standard CRC-32 check value

  asr650x::PersistState a = sample(), b;
  asr650x::persist_encode(a, buf);
  EXPECT_TRUE(asr650x::persist_decode(buf, b));
  EXPECT_TRUE(b.has_identity && memcmp(b.prv, a.prv, 64) == 0 && memcmp(b.pub, a.pub, 32) == 0);
  EXPECT_TRUE(b.has_prefs && strcmp(b.node_name, "KTM-test") == 0);
  EXPECT_TRUE(b.freq == 921.5f && b.bw == 62.5f && b.sf == 7 && b.cr == 5 && b.tx_power_dbm == 14);

  /* identity only / prefs only */
  a.has_prefs = false; asr650x::persist_encode(a, buf);
  EXPECT_TRUE(asr650x::persist_decode(buf, b) && b.has_identity && !b.has_prefs);
  a = sample(); a.has_identity = false; asr650x::persist_encode(a, buf);
  EXPECT_TRUE(asr650x::persist_decode(buf, b) && !b.has_identity && b.has_prefs);

  /* every single-byte corruption must be rejected and reset the state to empty */
  a = sample(); asr650x::persist_encode(a, buf);
  for (size_t i = 0; i < asr650x::PERSIST_SIZE; i++) {
    uint8_t bad[asr650x::PERSIST_SIZE]; memcpy(bad, buf, sizeof(bad));
    bad[i] ^= 0x01;
    asr650x::PersistState c = sample();
    EXPECT_TRUE(!asr650x::persist_decode(bad, c));
    EXPECT_TRUE(!c.has_identity && !c.has_prefs);
  }
  /* erased flash (all 0x00 / all 0xFF) is not a valid record */
  uint8_t z[asr650x::PERSIST_SIZE]; memset(z, 0x00, sizeof(z)); EXPECT_TRUE(!asr650x::persist_decode(z, b));
  memset(z, 0xFF, sizeof(z)); EXPECT_TRUE(!asr650x::persist_decode(z, b));
  /* a name without NUL terminator in the record is terminated on decode */
  a = sample(); memset(a.node_name, 'A', 32); asr650x::persist_encode(a, buf);
  EXPECT_TRUE(asr650x::persist_decode(buf, b) && b.node_name[31] == 0);
  /* GPS settings round-trip and are covered by the CRC */
  {
    asr650x::PersistState g = sample();
    g.gps_off = true; g.gps_interval = 3600;
    asr650x::persist_encode(g, buf);
    EXPECT_TRUE(buf[147] == 1);
    EXPECT_TRUE(buf[152] == (3600 & 0xFF) && buf[153] == ((3600 >> 8) & 0xFF) && buf[154] == 0 && buf[155] == 0);
    asr650x::PersistState h;
    EXPECT_TRUE(asr650x::persist_decode(buf, h) && h.gps_off && h.gps_interval == 3600);
    buf[153] ^= 1;
    EXPECT_TRUE(!asr650x::persist_decode(buf, h));                    // corrupting the interval byte is detected
  }
  /* a legacy record (bytes 147 and 152..155 are zero) reads as 'GPS on, interval 0' */
  {
    asr650x::PersistState legacy = sample();
    legacy.gps_off = false; legacy.gps_interval = 0;
    asr650x::persist_encode(legacy, buf);
    asr650x::PersistState r;
    EXPECT_TRUE(asr650x::persist_decode(buf, r) && !r.gps_off && r.gps_interval == 0);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
