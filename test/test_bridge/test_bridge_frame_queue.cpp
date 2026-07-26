#include <gtest/gtest.h>

#include <helpers/bridges/BridgeFrameQueue.h>

#include <string.h>
#include <vector>

namespace {

const uint16_t MAX_FRAME = 250;

std::vector<uint8_t> frameOf(size_t len, uint8_t tag) {
  std::vector<uint8_t> f(len);
  for (size_t i = 0; i < len; i++) {
    f[i] = (uint8_t)(tag + i);
  }
  return f;
}

}  // namespace

TEST(BridgeFrameQueue, StartsEmpty) {
  BridgeFrameQueue q(4, MAX_FRAME);

  EXPECT_TRUE(q.isEmpty());
  EXPECT_EQ(q.count(), 0);
  EXPECT_EQ(q.capacity(), 4);
}

TEST(BridgeFrameQueue, PopReturnsZeroWhenEmpty) {
  BridgeFrameQueue q(4, MAX_FRAME);
  uint8_t out[MAX_FRAME];

  EXPECT_EQ(q.pop(out, sizeof(out)), 0u);
}

TEST(BridgeFrameQueue, RoundTripsASingleFrame) {
  BridgeFrameQueue q(4, MAX_FRAME);
  auto f = frameOf(37, 0x10);

  ASSERT_TRUE(q.push(f.data(), f.size()));
  EXPECT_FALSE(q.isEmpty());
  EXPECT_EQ(q.count(), 1);

  uint8_t out[MAX_FRAME];
  ASSERT_EQ(q.pop(out, sizeof(out)), f.size());
  EXPECT_EQ(memcmp(out, f.data(), f.size()), 0);
  EXPECT_TRUE(q.isEmpty());
}

TEST(BridgeFrameQueue, PreservesFifoOrder) {
  BridgeFrameQueue q(4, MAX_FRAME);
  auto a = frameOf(10, 0xA0);
  auto b = frameOf(20, 0xB0);
  auto c = frameOf(30, 0xC0);

  ASSERT_TRUE(q.push(a.data(), a.size()));
  ASSERT_TRUE(q.push(b.data(), b.size()));
  ASSERT_TRUE(q.push(c.data(), c.size()));

  uint8_t out[MAX_FRAME];
  ASSERT_EQ(q.pop(out, sizeof(out)), a.size());
  EXPECT_EQ(memcmp(out, a.data(), a.size()), 0);
  ASSERT_EQ(q.pop(out, sizeof(out)), b.size());
  EXPECT_EQ(memcmp(out, b.data(), b.size()), 0);
  ASSERT_EQ(q.pop(out, sizeof(out)), c.size());
  EXPECT_EQ(memcmp(out, c.data(), c.size()), 0);
}

TEST(BridgeFrameQueue, AcceptsFramesUpToCapacity) {
  BridgeFrameQueue q(3, MAX_FRAME);
  auto f = frameOf(8, 0x01);

  EXPECT_TRUE(q.push(f.data(), f.size()));
  EXPECT_TRUE(q.push(f.data(), f.size()));
  EXPECT_TRUE(q.push(f.data(), f.size()));
  EXPECT_EQ(q.count(), 3);
}

// A full queue means the consumer is not keeping up. Loss is unavoidable there,
// but it must be counted so it is visible instead of silent.
TEST(BridgeFrameQueue, DropsAndCountsFramesPushedWhenFull) {
  BridgeFrameQueue q(2, MAX_FRAME);
  auto a = frameOf(8, 0xA0);
  auto b = frameOf(8, 0xB0);
  auto c = frameOf(8, 0xC0);

  ASSERT_TRUE(q.push(a.data(), a.size()));
  ASSERT_TRUE(q.push(b.data(), b.size()));

  EXPECT_FALSE(q.push(c.data(), c.size()));
  EXPECT_EQ(q.getDroppedFull(), 1u);
  EXPECT_EQ(q.count(), 2);

  // the frames already queued survive the overflow
  uint8_t out[MAX_FRAME];
  ASSERT_EQ(q.pop(out, sizeof(out)), a.size());
  EXPECT_EQ(memcmp(out, a.data(), a.size()), 0);
}

TEST(BridgeFrameQueue, RejectsAndCountsOversizeFrames) {
  BridgeFrameQueue q(4, 32);
  auto big = frameOf(33, 0x55);

  EXPECT_FALSE(q.push(big.data(), big.size()));
  EXPECT_EQ(q.getDroppedOversize(), 1u);
  EXPECT_TRUE(q.isEmpty());
}

TEST(BridgeFrameQueue, AcceptsFrameExactlyAtMaxFrameLen) {
  BridgeFrameQueue q(4, 32);
  auto exact = frameOf(32, 0x66);

  ASSERT_TRUE(q.push(exact.data(), exact.size()));

  uint8_t out[32];
  EXPECT_EQ(q.pop(out, sizeof(out)), 32u);
  EXPECT_EQ(q.getDroppedOversize(), 0u);
}

TEST(BridgeFrameQueue, RejectsAndCountsEmptyFrames) {
  BridgeFrameQueue q(4, MAX_FRAME);
  uint8_t dummy = 0;

  EXPECT_FALSE(q.push(&dummy, 0));
  EXPECT_FALSE(q.push(nullptr, 4));
  EXPECT_EQ(q.getDroppedOversize(), 2u);
  EXPECT_TRUE(q.isEmpty());
}

// If a frame will not fit the consumer's buffer, dropping it keeps the queue
// moving. Leaving it at the head would stall every frame behind it forever.
TEST(BridgeFrameQueue, DiscardsFrameThatDoesNotFitConsumerBufferInsteadOfWedging) {
  BridgeFrameQueue q(4, MAX_FRAME);
  auto big = frameOf(100, 0x70);
  auto small = frameOf(10, 0x80);

  ASSERT_TRUE(q.push(big.data(), big.size()));
  ASSERT_TRUE(q.push(small.data(), small.size()));

  uint8_t out[50];
  EXPECT_EQ(q.pop(out, sizeof(out)), 0u);
  EXPECT_EQ(q.getDroppedOversize(), 1u);

  // the next frame is still reachable
  EXPECT_EQ(q.pop(out, sizeof(out)), small.size());
  EXPECT_EQ(memcmp(out, small.data(), small.size()), 0);
}

TEST(BridgeFrameQueue, ReusesSlotsAfterDrainingSoItRunsIndefinitely) {
  BridgeFrameQueue q(3, MAX_FRAME);
  uint8_t out[MAX_FRAME];

  for (int round = 0; round < 20; round++) {
    auto f = frameOf(16, (uint8_t)round);
    ASSERT_TRUE(q.push(f.data(), f.size())) << "round " << round;
    ASSERT_EQ(q.pop(out, sizeof(out)), f.size()) << "round " << round;
    ASSERT_EQ(memcmp(out, f.data(), f.size()), 0) << "round " << round;
  }
  EXPECT_TRUE(q.isEmpty());
  EXPECT_EQ(q.getDroppedFull(), 0u);
}

TEST(BridgeFrameQueue, KeepsFramesIntactAcrossRingWrapAround) {
  BridgeFrameQueue q(3, MAX_FRAME);
  uint8_t out[MAX_FRAME];

  auto a = frameOf(11, 0xA0);
  auto b = frameOf(12, 0xB0);
  ASSERT_TRUE(q.push(a.data(), a.size()));
  ASSERT_TRUE(q.push(b.data(), b.size()));
  ASSERT_EQ(q.pop(out, sizeof(out)), a.size());

  // head now wraps past the end of the ring while b is still queued
  auto c = frameOf(13, 0xC0);
  auto d = frameOf(14, 0xD0);
  ASSERT_TRUE(q.push(c.data(), c.size()));
  ASSERT_TRUE(q.push(d.data(), d.size()));

  ASSERT_EQ(q.pop(out, sizeof(out)), b.size());
  EXPECT_EQ(memcmp(out, b.data(), b.size()), 0);
  ASSERT_EQ(q.pop(out, sizeof(out)), c.size());
  EXPECT_EQ(memcmp(out, c.data(), c.size()), 0);
  ASSERT_EQ(q.pop(out, sizeof(out)), d.size());
  EXPECT_EQ(memcmp(out, d.data(), d.size()), 0);
}

TEST(BridgeFrameQueue, TracksHighWaterMark) {
  BridgeFrameQueue q(4, MAX_FRAME);
  auto f = frameOf(8, 0x01);
  uint8_t out[MAX_FRAME];

  ASSERT_TRUE(q.push(f.data(), f.size()));
  ASSERT_TRUE(q.push(f.data(), f.size()));
  ASSERT_TRUE(q.push(f.data(), f.size()));
  EXPECT_EQ(q.getHighWaterMark(), 3);

  ASSERT_EQ(q.pop(out, sizeof(out)), f.size());
  ASSERT_EQ(q.pop(out, sizeof(out)), f.size());
  EXPECT_EQ(q.getHighWaterMark(), 3) << "high water mark must not fall when the queue drains";
}

TEST(BridgeFrameQueue, CountsPushesAndPops) {
  BridgeFrameQueue q(4, MAX_FRAME);
  auto f = frameOf(8, 0x01);
  uint8_t out[MAX_FRAME];

  ASSERT_TRUE(q.push(f.data(), f.size()));
  ASSERT_TRUE(q.push(f.data(), f.size()));
  ASSERT_EQ(q.pop(out, sizeof(out)), f.size());

  EXPECT_EQ(q.getPushed(), 2u);
  EXPECT_EQ(q.getPopped(), 1u);
}

TEST(BridgeFrameQueue, ResetStatsClearsCountersButKeepsQueuedFrames) {
  BridgeFrameQueue q(2, MAX_FRAME);
  auto f = frameOf(8, 0x01);

  ASSERT_TRUE(q.push(f.data(), f.size()));
  ASSERT_TRUE(q.push(f.data(), f.size()));
  ASSERT_FALSE(q.push(f.data(), f.size()));

  q.resetStats();

  EXPECT_EQ(q.getPushed(), 0u);
  EXPECT_EQ(q.getPopped(), 0u);
  EXPECT_EQ(q.getDroppedFull(), 0u);
  EXPECT_EQ(q.getDroppedOversize(), 0u);
  EXPECT_EQ(q.getHighWaterMark(), 0);
  EXPECT_EQ(q.count(), 2) << "resetting statistics must not discard queued frames";
}
