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

TEST(PacketParser, RejectsUnsupportedVersionAndPathMode) {
  EXPECT_FALSE(parse({0x40, 0x00}));
  EXPECT_FALSE(parse({0xff, 0x00}));
  EXPECT_FALSE(parse({0x3e, 0xc0}));
}

TEST(PacketParser, AllowsStructurallyValidEmptyRawPayload) {
  Packet packet;
  ASSERT_TRUE(parse({0x3e, 0x00}, &packet));
  EXPECT_EQ(0u, packet.path_len);
  EXPECT_EQ(0u, packet.payload_len);
}

TEST(PacketParser, RejectsTruncatedTypedPayloads) {
  EXPECT_FALSE(parse({0x2e, 0x00}));  // Empty CONTROL payload.
  EXPECT_FALSE(parse({0x26, 0x00}));  // Empty TRACE payload.
  EXPECT_FALSE(parse({0x0e, 0x00, 1, 2, 3}));  // Short ACK.

  // Direct encrypted payload: destination, source, MAC, then an incomplete
  // AES block. A valid packet must carry at least one complete block.
  std::vector<uint8_t> direct = {0x02, 0x00, 1, 2, 3, 4, 5};
  EXPECT_FALSE(parse(direct));
  direct.resize(2 + 2 + CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE, 0);
  EXPECT_TRUE(parse(direct));
}

TEST(PacketParser, EnforcesSupportedAckLengths) {
  EXPECT_TRUE(parse({0x0e, 0x00, 1, 2, 3, 4}));
  EXPECT_TRUE(parse({0x0e, 0x00, 1, 2, 3, 4, 5}));
  EXPECT_TRUE(parse({0x0e, 0x00, 1, 2, 3, 4, 5, 6}));
  EXPECT_FALSE(parse({0x0e, 0x00, 1, 2, 3, 4, 5, 6, 7}));

  // Multipart ACKs reserve payload[0] for their sequence/type byte.
  EXPECT_TRUE(parse({0x2a, 0x00, PAYLOAD_TYPE_ACK, 1, 2, 3, 4}));
  EXPECT_TRUE(parse({0x2a, 0x00, PAYLOAD_TYPE_ACK, 1, 2, 3, 4, 5, 6}));
  EXPECT_FALSE(parse({0x2a, 0x00, PAYLOAD_TYPE_ACK, 1, 2, 3, 4, 5, 6, 7}));
}

TEST(PacketParser, RejectsInvalidTraceShapeAndZeroHopControlRoute) {
  std::vector<uint8_t> trace(2 + 9, 0);
  trace[0] = 0x26;
  EXPECT_TRUE(parse(trace));

  trace[10] = 0x04;  // Reserved trace flag bit.
  EXPECT_FALSE(parse(trace));
  trace[10] = 0x01;  // Two-byte hashes, but one trailing hash byte.
  trace.push_back(0xaa);
  EXPECT_FALSE(parse(trace));

  EXPECT_TRUE(parse({0x2e, 0x00, 0x80}));
  EXPECT_FALSE(parse({0x2d, 0x00, 0x80}));
  EXPECT_FALSE(parse({0x2e, 0x01, 0xaa, 0x80}));
}

TEST(PacketParser, RejectsOverlongAdvertApplicationData) {
  std::vector<uint8_t> advert(2 + PUB_KEY_SIZE + sizeof(uint32_t) + SIGNATURE_SIZE,
                              0);
  advert[0] = 0x12;
  EXPECT_TRUE(parse(advert));

  advert.resize(advert.size() + MAX_ADVERT_DATA_SIZE, 0);
  EXPECT_TRUE(parse(advert));

  advert.push_back(0);
  EXPECT_FALSE(parse(advert));
}

TEST(PacketParser, RejectsTruncatedDecryptedPathPlaintext) {
  const uint8_t missing_path[] = {0x3f};
  EXPECT_FALSE(Packet::isValidPathPlaintext(missing_path, sizeof(missing_path)));

  const uint8_t missing_extra_type[] = {0x00};
  EXPECT_FALSE(Packet::isValidPathPlaintext(missing_extra_type,
                                            sizeof(missing_extra_type)));

  const uint8_t valid[] = {0x01, 0xaa, 0x00};
  EXPECT_TRUE(Packet::isValidPathPlaintext(valid, sizeof(valid)));
}

TEST(PacketParser, ValidatesLengthPrefixedPathFields) {
  EXPECT_FALSE(Packet::hasCompletePath(nullptr, 0));

  const uint8_t empty_path[] = {0x00};
  EXPECT_TRUE(Packet::hasCompletePath(empty_path, sizeof(empty_path)));

  const uint8_t truncated_path[] = {0x01};
  EXPECT_FALSE(Packet::hasCompletePath(truncated_path, sizeof(truncated_path)));

  const uint8_t complete_path[] = {0x01, 0xaa};
  EXPECT_TRUE(Packet::hasCompletePath(complete_path, sizeof(complete_path)));

  // Callers may pass authenticated AES padding after the complete path.
  const uint8_t path_with_trailing_data[] = {0x01, 0xaa, 0x00};
  EXPECT_TRUE(Packet::hasCompletePath(path_with_trailing_data,
                                      sizeof(path_with_trailing_data)));

  const uint8_t reserved_path_mode[] = {0xc0};
  EXPECT_FALSE(Packet::hasCompletePath(reserved_path_mode,
                                       sizeof(reserved_path_mode)));

  std::vector<uint8_t> maximum_path(1 + MAX_PATH_SIZE, 0xaa);
  maximum_path[0] = 0x60;  // 32 two-byte path hashes.
  EXPECT_TRUE(Packet::hasCompletePath(maximum_path.data(), maximum_path.size()));
  maximum_path.pop_back();
  EXPECT_FALSE(Packet::hasCompletePath(maximum_path.data(), maximum_path.size()));
}

TEST(PacketParser, SafelyRejectsArbitraryFramesAtEveryWireLength) {
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
