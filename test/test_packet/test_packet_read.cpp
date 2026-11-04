#include <gtest/gtest.h>
#include <vector>
#include "Packet.h"

using namespace mesh;

// Each input lives in an exactly-sized heap buffer so address sanitizers flag any read past 'len'.
static bool parse(const std::vector<uint8_t>& raw, Packet& p) {
  std::vector<uint8_t> exact(raw);
  exact.shrink_to_fit();
  return p.readFrom(exact.data(), (uint8_t)exact.size());
}

TEST(PacketReadFrom, RejectsEmptyAndHeaderOnly) {
  Packet p;
  uint8_t one = ROUTE_TYPE_FLOOD;
  EXPECT_FALSE(p.readFrom(&one, 0));
  EXPECT_FALSE(p.readFrom(&one, 1));
}

TEST(PacketReadFrom, RejectsTruncatedTransportCodes) {
  Packet p;
  for (size_t n = 1; n < 6; n++) {
    std::vector<uint8_t> raw(n, 0);
    raw[0] = ROUTE_TYPE_TRANSPORT_FLOOD;
    EXPECT_FALSE(parse(raw, p)) << "len=" << n;
  }
}

TEST(PacketReadFrom, RejectsPathLongerThanInput) {
  Packet p;
  // flood, path_len=10 (1-byte hashes) but only 3 bytes of path follow
  EXPECT_FALSE(parse({ROUTE_TYPE_FLOOD, 10, 1, 2, 3}, p));
  // transport variant
  EXPECT_FALSE(parse({ROUTE_TYPE_TRANSPORT_DIRECT, 1, 2, 3, 4, 10, 1, 2, 3}, p));
  // max path (63 x 1 byte hashes) with a truncated body
  std::vector<uint8_t> raw(10, 0);
  raw[0] = ROUTE_TYPE_FLOOD; raw[1] = 63;
  EXPECT_FALSE(parse(raw, p));
}

TEST(PacketReadFrom, RejectsPathWithNoPayload) {
  Packet p;
  EXPECT_FALSE(parse({ROUTE_TYPE_FLOOD, 2, 0xAA, 0xBB}, p));
  EXPECT_FALSE(parse({ROUTE_TYPE_FLOOD, 0}, p));
}

TEST(PacketReadFrom, RejectsInvalidPathLen) {
  Packet p;
  EXPECT_FALSE(parse({ROUTE_TYPE_FLOOD, 0xC0 | 1, 1, 2, 3, 4, 5}, p));  // reserved hash size
}

TEST(PacketReadFrom, AcceptsValidFloodPacket) {
  Packet p;
  ASSERT_TRUE(parse({ROUTE_TYPE_FLOOD, 2, 0xAA, 0xBB, 0x11, 0x22, 0x33}, p));
  EXPECT_EQ(2, p.path_len);
  EXPECT_EQ(0xAA, p.path[0]);
  EXPECT_EQ(0xBB, p.path[1]);
  ASSERT_EQ(3, p.payload_len);
  EXPECT_EQ(0x11, p.payload[0]);
  EXPECT_EQ(0x33, p.payload[2]);
}

TEST(PacketReadFrom, AcceptsValidTransportPacket) {
  Packet p;
  ASSERT_TRUE(parse({ROUTE_TYPE_TRANSPORT_FLOOD, 1, 0, 2, 0, 0, 0x7F}, p));
  EXPECT_EQ(0x0001, p.transport_codes[0]);
  EXPECT_EQ(0x0002, p.transport_codes[1]);
  EXPECT_EQ(0, p.path_len);
  ASSERT_EQ(1, p.payload_len);
  EXPECT_EQ(0x7F, p.payload[0]);
}

TEST(PacketReadFrom, RoundTripsWithWriteTo) {
  Packet a;
  ASSERT_TRUE(parse({ROUTE_TYPE_FLOOD, 3, 1, 2, 3, 9, 8, 7, 6}, a));
  uint8_t buf[256];
  uint8_t n = a.writeTo(buf);
  Packet b;
  ASSERT_TRUE(b.readFrom(buf, n));
  EXPECT_EQ(a.payload_len, b.payload_len);
  EXPECT_EQ(0, memcmp(a.payload, b.payload, a.payload_len));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
