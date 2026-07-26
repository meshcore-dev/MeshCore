#include <gtest/gtest.h>

#include <helpers/bridges/BridgeSerialFramer.h>

#include <string.h>
#include <vector>

namespace {

const uint16_t MAX_PAYLOAD = 256;

const uint8_t MAGIC_HI = (BridgeCodec::BRIDGE_PACKET_MAGIC >> 8) & 0xFF;
const uint8_t MAGIC_LO = BridgeCodec::BRIDGE_PACKET_MAGIC & 0xFF;

std::vector<uint8_t> payloadOf(size_t len, uint8_t tag) {
  std::vector<uint8_t> p(len);
  for (size_t i = 0; i < len; i++) {
    p[i] = (uint8_t)(tag + i * 3);
  }
  return p;
}

/** Builds a well-formed frame around a payload. */
std::vector<uint8_t> frameOf(const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> f;
  f.push_back(MAGIC_HI);
  f.push_back(MAGIC_LO);
  f.push_back((uint8_t)((payload.size() >> 8) & 0xFF));
  f.push_back((uint8_t)(payload.size() & 0xFF));
  f.insert(f.end(), payload.begin(), payload.end());
  const uint16_t csum = BridgeCodec::fletcher16(payload.data(), payload.size());
  f.push_back((uint8_t)((csum >> 8) & 0xFF));
  f.push_back((uint8_t)(csum & 0xFF));
  return f;
}

/** Feeds every byte, returning the length reported on the final byte. */
uint16_t feed(BridgeSerialFramer &fr, const std::vector<uint8_t> &bytes) {
  uint16_t got = 0;
  for (uint8_t b : bytes) {
    got = fr.offer(b);
  }
  return got;
}

}  // namespace

TEST(BridgeSerialFramer, ReportsNothingUntilAFrameIsComplete) {
  BridgeSerialFramer fr(MAX_PAYLOAD);
  auto payload = payloadOf(8, 0x10);
  auto frame = frameOf(payload);

  for (size_t i = 0; i + 1 < frame.size(); i++) {
    ASSERT_EQ(fr.offer(frame[i]), 0u) << "reported a frame early at byte " << i;
  }
  EXPECT_EQ(fr.offer(frame.back()), payload.size());
}

TEST(BridgeSerialFramer, DecodesACompleteFrame) {
  BridgeSerialFramer fr(MAX_PAYLOAD);
  auto payload = payloadOf(40, 0x20);

  ASSERT_EQ(feed(fr, frameOf(payload)), payload.size());
  EXPECT_EQ(memcmp(fr.payload(), payload.data(), payload.size()), 0);
  EXPECT_EQ(fr.getFramesDecoded(), 1u);
}

TEST(BridgeSerialFramer, DecodesBackToBackFrames) {
  BridgeSerialFramer fr(MAX_PAYLOAD);
  auto a = payloadOf(10, 0xA0);
  auto b = payloadOf(20, 0xB0);

  ASSERT_EQ(feed(fr, frameOf(a)), a.size());
  EXPECT_EQ(memcmp(fr.payload(), a.data(), a.size()), 0);

  ASSERT_EQ(feed(fr, frameOf(b)), b.size());
  EXPECT_EQ(memcmp(fr.payload(), b.data(), b.size()), 0);
  EXPECT_EQ(fr.getFramesDecoded(), 2u);
}

TEST(BridgeSerialFramer, SkipsAndCountsGarbageBeforeTheMagicHeader) {
  BridgeSerialFramer fr(MAX_PAYLOAD);
  auto payload = payloadOf(12, 0x30);

  std::vector<uint8_t> stream = { 0x00, 0x11, 0x22, 0x33 };
  auto frame = frameOf(payload);
  stream.insert(stream.end(), frame.begin(), frame.end());

  ASSERT_EQ(feed(fr, stream), payload.size());
  EXPECT_EQ(memcmp(fr.payload(), payload.data(), payload.size()), 0);
  EXPECT_EQ(fr.getResyncBytes(), 4u);
}

// Noise ending in the first magic byte must not eat the real header that follows.
TEST(BridgeSerialFramer, RecognisesTheHeaderWhenNoiseEndsWithTheFirstMagicByte) {
  BridgeSerialFramer fr(MAX_PAYLOAD);
  auto payload = payloadOf(6, 0x40);

  std::vector<uint8_t> stream = { MAGIC_HI, MAGIC_HI };
  auto frame = frameOf(payload);
  // drop the frame's own first magic byte; the second noise byte serves as it
  stream.insert(stream.end(), frame.begin() + 1, frame.end());

  ASSERT_EQ(feed(fr, stream), payload.size());
  EXPECT_EQ(memcmp(fr.payload(), payload.data(), payload.size()), 0);
}

TEST(BridgeSerialFramer, RejectsAndCountsAZeroLengthFrame) {
  BridgeSerialFramer fr(MAX_PAYLOAD);

  std::vector<uint8_t> stream = { MAGIC_HI, MAGIC_LO, 0x00, 0x00, 0x00, 0x00 };
  EXPECT_EQ(feed(fr, stream), 0u);
  EXPECT_EQ(fr.getLengthErrors(), 1u);
  EXPECT_EQ(fr.getFramesDecoded(), 0u);
}

TEST(BridgeSerialFramer, RejectsAndCountsAnOverlongLengthField) {
  BridgeSerialFramer fr(64);

  std::vector<uint8_t> stream = { MAGIC_HI, MAGIC_LO, 0x01, 0x00 };  // 256 > 64
  EXPECT_EQ(feed(fr, stream), 0u);
  EXPECT_EQ(fr.getLengthErrors(), 1u);
}

TEST(BridgeSerialFramer, RejectsAndCountsAChecksumMismatch) {
  BridgeSerialFramer fr(MAX_PAYLOAD);
  auto payload = payloadOf(16, 0x50);
  auto frame = frameOf(payload);
  frame[6] ^= 0x01;  // corrupt a payload byte

  EXPECT_EQ(feed(fr, frame), 0u);
  EXPECT_EQ(fr.getChecksumErrors(), 1u);
  EXPECT_EQ(fr.getFramesDecoded(), 0u);
}

// A rejected frame must not wedge the decoder: the next good frame still arrives.
TEST(BridgeSerialFramer, DecodesTheNextFrameAfterARejectedOne) {
  BridgeSerialFramer fr(MAX_PAYLOAD);
  auto bad = payloadOf(16, 0x50);
  auto bad_frame = frameOf(bad);
  bad_frame[6] ^= 0x01;
  ASSERT_EQ(feed(fr, bad_frame), 0u);

  auto good = payloadOf(24, 0x60);
  ASSERT_EQ(feed(fr, frameOf(good)), good.size());
  EXPECT_EQ(memcmp(fr.payload(), good.data(), good.size()), 0);
}

TEST(BridgeSerialFramer, DecodesTheNextFrameAfterABadLengthField) {
  BridgeSerialFramer fr(64);

  std::vector<uint8_t> junk = { MAGIC_HI, MAGIC_LO, 0x01, 0x00 };
  ASSERT_EQ(feed(fr, junk), 0u);

  auto good = payloadOf(20, 0x70);
  ASSERT_EQ(feed(fr, frameOf(good)), good.size());
  EXPECT_EQ(memcmp(fr.payload(), good.data(), good.size()), 0);
}

TEST(BridgeSerialFramer, AcceptsAPayloadExactlyAtTheMaximum) {
  BridgeSerialFramer fr(64);
  auto payload = payloadOf(64, 0x80);

  ASSERT_EQ(feed(fr, frameOf(payload)), payload.size());
  EXPECT_EQ(memcmp(fr.payload(), payload.data(), payload.size()), 0);
  EXPECT_EQ(fr.getLengthErrors(), 0u);
}

TEST(BridgeSerialFramer, ResetDiscardsAPartialFrame) {
  BridgeSerialFramer fr(MAX_PAYLOAD);
  auto payload = payloadOf(30, 0x90);
  auto frame = frameOf(payload);

  for (size_t i = 0; i < 10; i++) {
    fr.offer(frame[i]);
  }
  fr.reset();

  // the tail of the abandoned frame must not be mistaken for a frame
  for (size_t i = 10; i < frame.size(); i++) {
    ASSERT_EQ(fr.offer(frame[i]), 0u);
  }

  // a fresh frame still decodes
  ASSERT_EQ(feed(fr, frameOf(payload)), payload.size());
}

TEST(BridgeSerialFramer, ResetStatsClearsCounters) {
  BridgeSerialFramer fr(MAX_PAYLOAD);
  auto payload = payloadOf(8, 0xA0);
  ASSERT_EQ(feed(fr, frameOf(payload)), payload.size());
  ASSERT_EQ(fr.getFramesDecoded(), 1u);

  fr.resetStats();

  EXPECT_EQ(fr.getFramesDecoded(), 0u);
  EXPECT_EQ(fr.getChecksumErrors(), 0u);
  EXPECT_EQ(fr.getLengthErrors(), 0u);
  EXPECT_EQ(fr.getResyncBytes(), 0u);
}

TEST(BridgeSerialFramer, EncodeThenDecodeRoundTrips) {
  auto payload = payloadOf(50, 0xB0);
  uint8_t frame[512];

  const int frame_len = BridgeSerialFramer::encode(payload.data(), payload.size(), frame,
                                                   sizeof(frame));
  ASSERT_EQ(frame_len, (int)(payload.size() + BridgeSerialFramer::FRAME_OVERHEAD));

  BridgeSerialFramer fr(MAX_PAYLOAD);
  uint16_t got = 0;
  for (int i = 0; i < frame_len; i++) {
    got = fr.offer(frame[i]);
  }
  ASSERT_EQ(got, payload.size());
  EXPECT_EQ(memcmp(fr.payload(), payload.data(), payload.size()), 0);
}

TEST(BridgeSerialFramer, EncodeMatchesTheWireLayoutTheDecoderExpects) {
  auto payload = payloadOf(4, 0x11);
  uint8_t frame[64];
  const int frame_len = BridgeSerialFramer::encode(payload.data(), payload.size(), frame,
                                                   sizeof(frame));
  ASSERT_GT(frame_len, 0);

  EXPECT_EQ(frame[0], MAGIC_HI);
  EXPECT_EQ(frame[1], MAGIC_LO);
  EXPECT_EQ(frame[2], 0x00);
  EXPECT_EQ(frame[3], 0x04);
  EXPECT_EQ(memcmp(frame + 4, payload.data(), payload.size()), 0);
}

TEST(BridgeSerialFramer, EncodeRejectsPayloadTooLargeForCapacityWithoutWritingPastIt) {
  auto payload = payloadOf(100, 0xC0);

  uint8_t guarded[200];
  memset(guarded, 0xAA, sizeof(guarded));

  EXPECT_EQ(BridgeSerialFramer::encode(payload.data(), payload.size(), guarded, 100), -1);
  for (size_t i = 0; i < sizeof(guarded); i++) {
    ASSERT_EQ(guarded[i], 0xAA) << "encode() wrote to the buffer at offset " << i;
  }
}

TEST(BridgeSerialFramer, EncodeRejectsEmptyPayload) {
  uint8_t frame[64];
  EXPECT_EQ(BridgeSerialFramer::encode(nullptr, 0, frame, sizeof(frame)), -1);
}
