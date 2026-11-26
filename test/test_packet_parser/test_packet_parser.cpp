#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "Packet.h"

using mesh::Packet;

namespace {

bool parse(const std::vector<uint8_t>& encoded, Packet* packet = nullptr) {
  Packet local;
  return (packet == nullptr ? local : *packet).readFrom(encoded.data(), encoded.size());
}

}  // namespace

TEST(PacketParser, RejectsTruncatedFramePrefixes) {
  EXPECT_FALSE(parse({}));
  EXPECT_FALSE(parse({0x0d}));  // Header without path length.

  // Transport-scoped header requires two complete 16-bit transport codes and
  // a path-length byte.
  EXPECT_FALSE(parse({0x0c}));
  EXPECT_FALSE(parse({0x0c, 0x00}));
  EXPECT_FALSE(parse({0x0c, 0x00, 0x00}));
  EXPECT_FALSE(parse({0x0c, 0x00, 0x00, 0x00}));
  EXPECT_FALSE(parse({0x0c, 0x00, 0x00, 0x00, 0x00}));

  // One one-byte path hash is declared but absent.
  EXPECT_FALSE(parse({0x0d, 0x01}));
}

TEST(PacketParser, RejectsUnsupportedPathMode) {
  EXPECT_FALSE(parse({0x3e, 0xc0}));
}

TEST(PacketParser, TreatsPayloadVersionAndContentsAsOpaqueBytes) {
  Packet packet;
  ASSERT_TRUE(parse({0x3e, 0x00}, &packet));
  EXPECT_EQ(0u, packet.path_len);
  EXPECT_EQ(0u, packet.payload_len);

  ASSERT_TRUE(parse({0xfe, 0x00, 0xaa, 0xbb, 0xcc}, &packet));
  EXPECT_EQ(PAYLOAD_VER_4, packet.getPayloadVer());
  ASSERT_EQ(3u, packet.payload_len);
  EXPECT_EQ(0xaa, packet.payload[0]);
  EXPECT_EQ(0xbb, packet.payload[1]);
  EXPECT_EQ(0xcc, packet.payload[2]);
}

TEST(PacketParser, SafelyHandlesArbitraryFramesAtEveryWireLength) {
  uint32_t state = 0x6d657368;
  for (size_t len = 0; len <= MAX_TRANS_UNIT; ++len) {
    for (int sample = 0; sample < 32; ++sample) {
      std::vector<uint8_t> input(len);
      for (uint8_t& byte : input) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<uint8_t>(state >> 24);
      }
      Packet packet;
      (void)packet.readFrom(input.data(), input.size());
    }
  }
}

TEST(PacketParser, EnforcesPathAndPayloadBounds) {
  // 63 two-byte hashes exceed MAX_PATH_SIZE.
  EXPECT_FALSE(parse({0x3e, 0x7f}));

  std::vector<uint8_t> maximum = {0x3e, 0x00};
  maximum.resize(2 + MAX_PACKET_PAYLOAD, 0xaa);
  EXPECT_TRUE(parse(maximum));

  maximum.push_back(0xaa);
  EXPECT_FALSE(parse(maximum));
}

TEST(PacketParser, AcceptsCompleteTransportScopedPacket) {
  Packet packet;
  ASSERT_TRUE(parse({0x0f, 0x34, 0x12, 0x78, 0x56, 0x01, 0xaa,
                     0x01, 0x02, 0x03, 0x04},
                    &packet));
  EXPECT_EQ(0x1234u, packet.transport_codes[0]);
  EXPECT_EQ(0x5678u, packet.transport_codes[1]);
  EXPECT_EQ(1u, packet.getPathByteLen());
  EXPECT_EQ(4u, packet.payload_len);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
