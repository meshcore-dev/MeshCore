#include <gtest/gtest.h>
#include <string.h>

#define CONTACT_OUT_PATH_SIZE 16
#include "helpers/ContactPath.h"

// encoded path_len: upper 2 bits = hash size - 1, lower 6 bits = hop count
static uint8_t enc(uint8_t hash_size, uint8_t count) { return (uint8_t)(((hash_size - 1) << 6) | count); }

TEST(ContactPath, ShortPathIsStored) {
  uint8_t src[64], dst[CONTACT_OUT_PATH_SIZE + 1];
  for (int i = 0; i < 64; i++) src[i] = (uint8_t)(i + 1);
  dst[CONTACT_OUT_PATH_SIZE] = 0xAA;  // guard byte
  EXPECT_EQ(contactStorePath(dst, src, enc(1, 5)), enc(1, 5));
  EXPECT_EQ(memcmp(dst, src, 5), 0);
  EXPECT_EQ(dst[CONTACT_OUT_PATH_SIZE], 0xAA);
}

TEST(ContactPath, ExactlyFullIsStored) {
  uint8_t src[64] = {0}, dst[CONTACT_OUT_PATH_SIZE + 1];
  dst[CONTACT_OUT_PATH_SIZE] = 0xAA;
  EXPECT_EQ(contactStorePath(dst, src, enc(2, 8)), enc(2, 8));  // 8 hops x 2 bytes = 16
  EXPECT_EQ(dst[CONTACT_OUT_PATH_SIZE], 0xAA);
}

TEST(ContactPath, TooLongBecomesUnknown) {
  uint8_t src[64] = {0}, dst[CONTACT_OUT_PATH_SIZE + 1];
  dst[CONTACT_OUT_PATH_SIZE] = 0xAA;
  for (uint8_t hops = 17; hops <= 63; hops++) {  // 1-byte hashes
    EXPECT_EQ(contactStorePath(dst, src, enc(1, hops)), OUT_PATH_UNKNOWN) << (int)hops;
  }
  EXPECT_EQ(contactStorePath(dst, src, enc(3, 6)), OUT_PATH_UNKNOWN);  // 18 bytes
  EXPECT_EQ(dst[CONTACT_OUT_PATH_SIZE], 0xAA);
}

TEST(ContactPath, ReservedHashSizeBecomesUnknown) {
  uint8_t src[64] = {0}, dst[CONTACT_OUT_PATH_SIZE + 1];
  dst[CONTACT_OUT_PATH_SIZE] = 0xAA;
  EXPECT_EQ(contactStorePath(dst, src, enc(4, 2)), OUT_PATH_UNKNOWN);  // hash size 4 is reserved
  EXPECT_EQ(dst[CONTACT_OUT_PATH_SIZE], 0xAA);
}

TEST(ContactPath, UnknownStaysUnknown) {
  uint8_t src[64] = {0}, dst[CONTACT_OUT_PATH_SIZE];
  EXPECT_EQ(contactStorePath(dst, src, OUT_PATH_UNKNOWN), OUT_PATH_UNKNOWN);
}

TEST(ContactPath, FrameAlwaysGets64Bytes) {
  uint8_t stored[CONTACT_OUT_PATH_SIZE], frame[65];
  for (int i = 0; i < CONTACT_OUT_PATH_SIZE; i++) stored[i] = (uint8_t)(0x10 + i);
  memset(frame, 0xEE, sizeof(frame));
  contactWritePath64(frame, stored, enc(1, 4));
  EXPECT_EQ(memcmp(frame, stored, 4), 0);
  for (int i = 4; i < 64; i++) EXPECT_EQ(frame[i], 0) << i;  // nothing stale after the route
  EXPECT_EQ(frame[64], 0xEE);  // nothing written past 64
}

TEST(ContactPath, FrameForUnknownRouteIsAllZero) {
  uint8_t stored[CONTACT_OUT_PATH_SIZE], frame[64];
  memset(stored, 0x55, sizeof(stored));
  memset(frame, 0xEE, sizeof(frame));
  contactWritePath64(frame, stored, OUT_PATH_UNKNOWN);
  for (int i = 0; i < 64; i++) EXPECT_EQ(frame[i], 0) << i;
}

// call-site wrappers (small mode): from a 64-byte frame/file field, from a packet route, and back to 64 bytes
TEST(ContactPath, WrappersClampInSmallMode) {
  uint8_t src[64], dst[CONTACT_OUT_PATH_SIZE + 1], out[64];
  for (int i = 0; i < 64; i++) src[i] = (uint8_t)(0x30 + i);
  dst[CONTACT_OUT_PATH_SIZE] = 0xAA;
  EXPECT_EQ(contactPathFrom64(dst, src, enc(1, 3)), enc(1, 3));
  EXPECT_EQ(memcmp(dst, src, 3), 0);
  EXPECT_EQ(contactPathFrom64(dst, src, enc(1, 40)), OUT_PATH_UNKNOWN);
  EXPECT_EQ(contactPathFromPacket(dst, src, enc(2, 9)), OUT_PATH_UNKNOWN);   // 18 bytes
  EXPECT_EQ(contactPathFromPacket(dst, src, enc(2, 8)), enc(2, 8));
  EXPECT_EQ(dst[CONTACT_OUT_PATH_SIZE], 0xAA);
  contactPathTo64(out, dst, enc(2, 8));
  EXPECT_EQ(memcmp(out, src, 16), 0);
  for (int i = 16; i < 64; i++) EXPECT_EQ(out[i], 0) << i;
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
