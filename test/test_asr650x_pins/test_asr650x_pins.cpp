#include "helpers/asr650x/PinnedContacts.h"

#include <gtest/gtest.h>
#include <string.h>

static void key(uint8_t k[32], uint8_t v) {
  for (int i = 0; i < 32; i++)
    k[i] = (uint8_t)(v + i);
}

TEST(Asr650xPins, ErasedOrGarbageRowIsEmpty) {
  uint8_t row[asr650x::PIN_SIZE];
  asr650x::PinSet s;
  memset(row, 0xFF, sizeof(row));
  EXPECT_FALSE(asr650x::pinsDecode(row, s));
  EXPECT_EQ(s.n, 0);
  memset(row, 0x00, sizeof(row));
  EXPECT_FALSE(asr650x::pinsDecode(row, s));
  EXPECT_EQ(s.n, 0);
}

TEST(Asr650xPins, AtMostThreeAndRepinOnlyRefreshes) {
  uint8_t a[32], b[32], c[32], d[32];
  key(a, 0x10);
  key(b, 0x40);
  key(c, 0x70);
  key(d, 0xA0);
  asr650x::PinSet s;
  EXPECT_EQ(asr650x::pinUpsert(s, a, "nRF-A", 1, 0), asr650x::PIN_CHANGED);
  EXPECT_EQ(asr650x::pinUpsert(s, b, "nRF-B", 1, 0), asr650x::PIN_CHANGED);
  EXPECT_EQ(asr650x::pinUpsert(s, c, "room", 3, 1), asr650x::PIN_CHANGED);
  EXPECT_EQ(s.n, 3);
  EXPECT_EQ(asr650x::pinUpsert(s, d, "fourth", 1, 0), asr650x::PIN_FULL);
  EXPECT_EQ(s.n, 3);
  EXPECT_EQ(asr650x::pinUpsert(s, b, "nRF-B", 1, 0), asr650x::PIN_SAME);
  EXPECT_EQ(asr650x::pinUpsert(s, b, "nRF-B2", 1, 0), asr650x::PIN_CHANGED);
  EXPECT_STREQ(s.e[1].name, "nRF-B2");
  EXPECT_LT(asr650x::pinFind(s, d), 0);
  EXPECT_EQ(asr650x::pinFind(s, c), 2);
}

TEST(Asr650xPins, RefreshNeverPinsANewContact) {
  uint8_t a[32], d[32];
  key(a, 0x10);
  key(d, 0xA0);
  asr650x::PinSet s;
  asr650x::pinUpsert(s, a, "A", 1, 0);
  EXPECT_EQ(asr650x::pinRefresh(s, d, "D", 1, 0), asr650x::PIN_SAME);
  EXPECT_EQ(s.n, 1);
  EXPECT_EQ(asr650x::pinRefresh(s, a, "A-new", 2, 4), asr650x::PIN_CHANGED);
  EXPECT_EQ(s.e[0].type, 2);
  EXPECT_EQ(s.e[0].flags, 4);
}

TEST(Asr650xPins, RemoveKeepsOrder) {
  uint8_t a[32], b[32], c[32], d[32];
  key(a, 0x10);
  key(b, 0x40);
  key(c, 0x70);
  key(d, 0xA0);
  asr650x::PinSet s;
  asr650x::pinUpsert(s, a, "A", 1, 0);
  asr650x::pinUpsert(s, b, "B", 1, 0);
  asr650x::pinUpsert(s, c, "C", 1, 0);
  EXPECT_TRUE(asr650x::pinRemove(s, b));
  EXPECT_EQ(s.n, 2);
  EXPECT_EQ(asr650x::pinFind(s, a), 0);
  EXPECT_EQ(asr650x::pinFind(s, c), 1);
  EXPECT_FALSE(asr650x::pinRemove(s, d));
  EXPECT_EQ(s.n, 2);
}

TEST(Asr650xPins, RoundTripAndEveryByteCovered) {
  uint8_t a[32], c[32];
  key(a, 0x10);
  key(c, 0x70);
  asr650x::PinSet s, t;
  asr650x::pinUpsert(s, a, "A-VERY-LONG-NODE-NAME-THAT-DOES-NOT-FIT", 1, 2);
  asr650x::pinUpsert(s, c, "C", 3, 0);
  uint8_t row[asr650x::PIN_SIZE];
  asr650x::pinsEncode(s, row);
  ASSERT_TRUE(asr650x::pinsDecode(row, t));
  EXPECT_EQ(t.n, 2);
  EXPECT_EQ(memcmp(t.e[0].pub, a, 32), 0);
  EXPECT_EQ(strlen(t.e[0].name), 31u);
  EXPECT_EQ(t.e[0].type, 1);
  EXPECT_EQ(t.e[0].flags, 2);
  EXPECT_STREQ(t.e[1].name, "C");
  for (size_t i = 0; i < asr650x::PIN_SIZE; i++) {
    uint8_t bad[asr650x::PIN_SIZE];
    memcpy(bad, row, sizeof(bad));
    bad[i] ^= 0x01;
    asr650x::PinSet u;
    EXPECT_FALSE(asr650x::pinsDecode(bad, u)) << i;
    EXPECT_EQ(u.n, 0);
  }
}

TEST(Asr650xPins, CountAboveThreeRejectedEvenWithValidCrc) {
  uint8_t a[32];
  key(a, 0x10);
  asr650x::PinSet s;
  asr650x::pinUpsert(s, a, "A", 1, 0);
  uint8_t row[asr650x::PIN_SIZE];
  asr650x::pinsEncode(s, row);
  row[4] = 4;
  uint32_t crc = asr650x::crc32(row, asr650x::PIN_SIZE - 4);
  for (int i = 0; i < 4; i++)
    row[asr650x::PIN_SIZE - 4 + i] = (uint8_t)(crc >> (8 * i));
  asr650x::PinSet t;
  EXPECT_FALSE(asr650x::pinsDecode(row, t));
  EXPECT_EQ(t.n, 0);
}

TEST(Asr650xPins, EmptySetIsAValidRecord) {
  asr650x::PinSet s, t;
  uint8_t row[asr650x::PIN_SIZE];
  asr650x::pinsEncode(s, row);
  EXPECT_TRUE(asr650x::pinsDecode(row, t));
  EXPECT_EQ(t.n, 0);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
