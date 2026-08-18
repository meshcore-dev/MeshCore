#include <gtest/gtest.h>

#include <helpers/bridges/BridgeCodec.h>

#include <string.h>
#include <vector>

namespace {

const char *SECRET = "LVSITANOS";

std::vector<uint8_t> makePayload(size_t len) {
  std::vector<uint8_t> p(len);
  for (size_t i = 0; i < len; i++) {
    p[i] = (uint8_t)(i * 7 + 3);
  }
  return p;
}

}  // namespace

TEST(BridgeCodec, RoundTripsPayload) {
  auto payload = makePayload(64);
  uint8_t frame[250];

  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), SECRET, frame, sizeof(frame));
  ASSERT_EQ(frame_len, (int)(payload.size() + BridgeCodec::FRAME_OVERHEAD));

  uint8_t out[250];
  int out_len = BridgeCodec::decode(frame, frame_len, SECRET, out, sizeof(out));

  ASSERT_EQ(out_len, (int)payload.size());
  EXPECT_EQ(memcmp(out, payload.data(), payload.size()), 0);
}

TEST(BridgeCodec, EncodedFrameStartsWithClearTextMagic) {
  auto payload = makePayload(16);
  uint8_t frame[250];

  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), SECRET, frame, sizeof(frame));
  ASSERT_GT(frame_len, 0);

  EXPECT_EQ(frame[0], (BridgeCodec::BRIDGE_PACKET_MAGIC >> 8) & 0xFF);
  EXPECT_EQ(frame[1], BridgeCodec::BRIDGE_PACKET_MAGIC & 0xFF);
}

TEST(BridgeCodec, EncodedPayloadIsNotSentInClear) {
  auto payload = makePayload(32);
  uint8_t frame[250];

  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), SECRET, frame, sizeof(frame));
  ASSERT_GT(frame_len, 0);

  EXPECT_NE(memcmp(frame + BridgeCodec::FRAME_OVERHEAD, payload.data(), payload.size()), 0);
}

// The largest blob Packet::writeTo() can emit is 253 bytes, which does not fit an
// ESP-NOW frame. encode() must refuse it rather than smash the caller's buffer.
TEST(BridgeCodec, EncodeRejectsPayloadTooLargeForCapacityWithoutWritingPastIt) {
  auto payload = makePayload(253);

  uint8_t guarded[300];
  memset(guarded, 0xAA, sizeof(guarded));
  const size_t cap = 250;

  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), SECRET, guarded, cap);

  EXPECT_EQ(frame_len, -1);
  for (size_t i = 0; i < sizeof(guarded); i++) {
    ASSERT_EQ(guarded[i], 0xAA) << "encode() wrote to the buffer at offset " << i;
  }
}

TEST(BridgeCodec, EncodeAcceptsPayloadThatExactlyFillsCapacity) {
  auto payload = makePayload(250 - BridgeCodec::FRAME_OVERHEAD);
  uint8_t frame[250];

  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), SECRET, frame, sizeof(frame));

  ASSERT_EQ(frame_len, 250);

  uint8_t out[250];
  ASSERT_EQ(BridgeCodec::decode(frame, frame_len, SECRET, out, sizeof(out)), (int)payload.size());
  EXPECT_EQ(memcmp(out, payload.data(), payload.size()), 0);
}

TEST(BridgeCodec, DecodeRejectsWrongMagic) {
  auto payload = makePayload(20);
  uint8_t frame[250];
  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), SECRET, frame, sizeof(frame));
  ASSERT_GT(frame_len, 0);

  frame[0] ^= 0xFF;

  uint8_t out[250];
  EXPECT_EQ(BridgeCodec::decode(frame, frame_len, SECRET, out, sizeof(out)), -1);
}

TEST(BridgeCodec, DecodeRejectsCorruptedPayload) {
  auto payload = makePayload(20);
  uint8_t frame[250];
  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), SECRET, frame, sizeof(frame));
  ASSERT_GT(frame_len, 0);

  frame[frame_len - 1] ^= 0x01;

  uint8_t out[250];
  EXPECT_EQ(BridgeCodec::decode(frame, frame_len, SECRET, out, sizeof(out)), -1);
}

TEST(BridgeCodec, DecodeRejectsFrameFromANetworkWithADifferentSecret) {
  auto payload = makePayload(20);
  uint8_t frame[250];
  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), SECRET, frame, sizeof(frame));
  ASSERT_GT(frame_len, 0);

  uint8_t out[250];
  EXPECT_EQ(BridgeCodec::decode(frame, frame_len, "some-other-net", out, sizeof(out)), -1);
}

TEST(BridgeCodec, DecodeRejectsTruncatedFrame) {
  uint8_t frame[3] = { 0xC0, 0x3E, 0x00 };
  uint8_t out[250];

  EXPECT_EQ(BridgeCodec::decode(frame, sizeof(frame), SECRET, out, sizeof(out)), -1);
  EXPECT_EQ(BridgeCodec::decode(frame, 0, SECRET, out, sizeof(out)), -1);
}

TEST(BridgeCodec, DecodeRejectsFrameCarryingNoPayload) {
  uint8_t frame[BridgeCodec::FRAME_OVERHEAD];
  int frame_len = BridgeCodec::encode(nullptr, 0, SECRET, frame, sizeof(frame));
  // An empty payload is never a valid mesh packet, so it must not encode either.
  EXPECT_EQ(frame_len, -1);

  uint8_t only_header[4] = { 0xC0, 0x3E, 0x00, 0x00 };
  uint8_t out[250];
  EXPECT_EQ(BridgeCodec::decode(only_header, sizeof(only_header), SECRET, out, sizeof(out)), -1);
}

TEST(BridgeCodec, DecodeRejectsPayloadLargerThanOutputCapacity) {
  auto payload = makePayload(100);
  uint8_t frame[250];
  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), SECRET, frame, sizeof(frame));
  ASSERT_GT(frame_len, 0);

  uint8_t small[50];
  EXPECT_EQ(BridgeCodec::decode(frame, frame_len, SECRET, small, sizeof(small)), -1);
}

// 'set bridge.secret ' with a blank argument leaves an empty string in prefs.
// A modulo by strlen(secret) would divide by zero here.
TEST(BridgeCodec, EmptySecretRoundTripsInsteadOfDividingByZero) {
  auto payload = makePayload(20);
  uint8_t frame[250];

  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), "", frame, sizeof(frame));
  ASSERT_EQ(frame_len, (int)(payload.size() + BridgeCodec::FRAME_OVERHEAD));

  uint8_t out[250];
  ASSERT_EQ(BridgeCodec::decode(frame, frame_len, "", out, sizeof(out)), (int)payload.size());
  EXPECT_EQ(memcmp(out, payload.data(), payload.size()), 0);
}

TEST(BridgeCodec, NullSecretRoundTripsInsteadOfDereferencingNull) {
  auto payload = makePayload(20);
  uint8_t frame[250];

  int frame_len = BridgeCodec::encode(payload.data(), payload.size(), nullptr, frame, sizeof(frame));
  ASSERT_EQ(frame_len, (int)(payload.size() + BridgeCodec::FRAME_OVERHEAD));

  uint8_t out[250];
  ASSERT_EQ(BridgeCodec::decode(frame, frame_len, nullptr, out, sizeof(out)), (int)payload.size());
  EXPECT_EQ(memcmp(out, payload.data(), payload.size()), 0);
}

TEST(BridgeCodec, XorCryptIsItsOwnInverse) {
  auto payload = makePayload(40);
  std::vector<uint8_t> buf = payload;

  BridgeCodec::xorCrypt(buf.data(), buf.size(), SECRET);
  EXPECT_NE(memcmp(buf.data(), payload.data(), payload.size()), 0);

  BridgeCodec::xorCrypt(buf.data(), buf.size(), SECRET);
  EXPECT_EQ(memcmp(buf.data(), payload.data(), payload.size()), 0);
}

TEST(BridgeCodec, Fletcher16MatchesKnownValue) {
  const uint8_t abcde[] = { 'a', 'b', 'c', 'd', 'e' };
  EXPECT_EQ(BridgeCodec::fletcher16(abcde, sizeof(abcde)), 0xC8F0);
}
