#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "helpers/ChannelHistory.h"

// Build a fake GRP_TXT payload: raw[0] = channel hash, followed by a body. The real
// firmware never inspects the body (it's ciphertext); these tests only care that the
// bytes round-trip verbatim and that dedup is by exact content.
static std::vector<uint8_t> mkPkt(uint8_t channel_hash, const std::string& body) {
  std::vector<uint8_t> p;
  p.push_back(channel_hash);
  p.insert(p.end(), body.begin(), body.end());
  return p;
}

static bool cap(ChannelHistory<8, 64>& h, uint8_t chan, const std::string& body, uint32_t ts) {
  auto p = mkPkt(chan, body);
  return h.capture(p.data(), (uint16_t)p.size(), ts);
}

// -------- capture / dedup --------

TEST(ChannelHistory, CapturesAndCounts) {
  ChannelHistory<8, 64> h;
  EXPECT_EQ(h.countStored(), 0);
  EXPECT_TRUE(cap(h, 0xAA, "hello", 100));
  EXPECT_EQ(h.countStored(), 1);
}

TEST(ChannelHistory, DedupsIdenticalReflood) {
  ChannelHistory<8, 64> h;
  EXPECT_TRUE (cap(h, 0xAA, "same-bytes", 100));
  EXPECT_FALSE(cap(h, 0xAA, "same-bytes", 105));  // re-flood heard from another neighbour
  EXPECT_FALSE(cap(h, 0xAA, "same-bytes", 110));
  EXPECT_EQ(h.countStored(), 1);
}

TEST(ChannelHistory, DistinctMessagesBothStored) {
  ChannelHistory<8, 64> h;
  EXPECT_TRUE(cap(h, 0xAA, "msg-one", 100));
  EXPECT_TRUE(cap(h, 0xAA, "msg-two", 101));      // same channel, different body
  EXPECT_TRUE(cap(h, 0xBB, "msg-one", 102));      // same body, different channel prefix
  EXPECT_EQ(h.countStored(), 3);
}

TEST(ChannelHistory, RejectsEmptyAndOversize) {
  ChannelHistory<8, 64> h;
  uint8_t one = 0x01;
  EXPECT_FALSE(h.capture(&one, 0, 100));          // empty
  std::vector<uint8_t> big(65, 0x7);              // > MAX_RAW (64)
  EXPECT_FALSE(h.capture(big.data(), (uint16_t)big.size(), 100));
  std::vector<uint8_t> fits(64, 0x7);             // exactly MAX_RAW
  EXPECT_TRUE(h.capture(fits.data(), (uint16_t)fits.size(), 100));
  EXPECT_EQ(h.countStored(), 1);
}

// -------- ring wrap / eviction --------

TEST(ChannelHistory, EvictsOldestOnWrap) {
  ChannelHistory<4, 64> h;
  for (int i = 0; i < 6; i++) {                   // 6 into a depth-4 ring
    auto p = mkPkt(0xAA, "m" + std::to_string(i));
    ASSERT_TRUE(h.capture(p.data(), (uint16_t)p.size(), 100 + i));
  }
  EXPECT_EQ(h.countStored(), 4);                  // capped at depth
  uint32_t ts = 0;
  ASSERT_TRUE(h.entryAt(0, nullptr, nullptr, &ts));
  EXPECT_EQ(ts, 102u);                            // m0,m1 evicted; oldest survivor is m2
}

// -------- serve --------

// Decode a serve() buffer into (recv_ts, raw) records for assertions.
struct Rec { uint32_t ts; std::vector<uint8_t> raw; };
static std::vector<Rec> decode(const uint8_t* buf, int len) {
  std::vector<Rec> out;
  int ofs = 0;
  while (ofs + 5 <= len) {
    Rec r;
    memcpy(&r.ts, &buf[ofs], 4); ofs += 4;
    uint8_t rl = buf[ofs++];
    r.raw.assign(&buf[ofs], &buf[ofs + rl]); ofs += rl;
    out.push_back(r);
  }
  return out;
}

TEST(ChannelHistory, ServeReturnsNewerThanSinceInOrder) {
  ChannelHistory<8, 64> h;
  cap(h, 0xAA, "a", 100);
  cap(h, 0xAA, "b", 200);
  cap(h, 0xAA, "c", 300);

  uint8_t buf[256]; uint8_t count = 0;
  int n = h.serve(0xFF, 150, 0, buf, sizeof(buf), count);
  auto recs = decode(buf, n);
  ASSERT_EQ(count, 2);
  ASSERT_EQ(recs.size(), 2u);
  EXPECT_EQ(recs[0].ts, 200u);                    // oldest-first, and 'a'@100 excluded by since=150
  EXPECT_EQ(recs[1].ts, 300u);
  EXPECT_EQ(std::string(recs[1].raw.begin() + 1, recs[1].raw.end()), "c");
}

TEST(ChannelHistory, ServeFiltersByChannelHash) {
  ChannelHistory<8, 64> h;
  cap(h, 0xAA, "on-aa", 100);
  cap(h, 0xBB, "on-bb", 101);
  cap(h, 0xAA, "on-aa-2", 102);

  uint8_t buf[256]; uint8_t count = 0;
  int n = h.serve(0xAA, 0, 0, buf, sizeof(buf), count);
  auto recs = decode(buf, n);
  ASSERT_EQ(count, 2);
  for (auto& r : recs) EXPECT_EQ(r.raw[0], 0xAA);
}

TEST(ChannelHistory, ServeRespectsMaxCount) {
  ChannelHistory<8, 64> h;
  for (int i = 0; i < 5; i++) cap(h, 0xAA, "m" + std::to_string(i), 100 + i);
  uint8_t buf[256]; uint8_t count = 0;
  h.serve(0xFF, 0, 2, buf, sizeof(buf), count);
  EXPECT_EQ(count, 2);
}

TEST(ChannelHistory, ServePagesByAdvancingSince) {
  ChannelHistory<8, 64> h;
  for (int i = 0; i < 5; i++) cap(h, 0xAA, "m" + std::to_string(i), 100 + i);

  uint8_t buf[256]; uint8_t count = 0;
  int n = h.serve(0xFF, 0, 2, buf, sizeof(buf), count);   // page 1
  auto page1 = decode(buf, n);
  ASSERT_EQ(count, 2);
  uint32_t cursor = page1.back().ts;

  n = h.serve(0xFF, cursor, 2, buf, sizeof(buf), count);  // page 2
  auto page2 = decode(buf, n);
  ASSERT_EQ(count, 2);
  EXPECT_GT(page2.front().ts, cursor);                    // strictly newer; no overlap
}

TEST(ChannelHistory, ServeStopsWhenBufferFull) {
  ChannelHistory<8, 64> h;
  std::vector<uint8_t> body(60, 0x9);                     // record = 5 + 61 = 66 bytes
  for (int i = 0; i < 4; i++) {
    std::vector<uint8_t> p; p.push_back(0xAA);
    p.insert(p.end(), body.begin(), body.end());
    p[1] = (uint8_t)i;                                    // make each distinct
    ASSERT_TRUE(h.capture(p.data(), (uint16_t)p.size(), 100 + i));
  }
  uint8_t buf[140]; uint8_t count = 0;                    // fits only 2 records (132 bytes)
  int n = h.serve(0xFF, 0, 0, buf, sizeof(buf), count);
  EXPECT_EQ(count, 2);                                    // partial; client pages for the rest
  EXPECT_LE(n, (int)sizeof(buf));
}

TEST(ChannelHistory, ClearEmpties) {
  ChannelHistory<8, 64> h;
  cap(h, 0xAA, "x", 100);
  h.clear();
  EXPECT_EQ(h.countStored(), 0);
  uint8_t buf[64]; uint8_t count = 0;
  h.serve(0xFF, 0, 0, buf, sizeof(buf), count);
  EXPECT_EQ(count, 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
