#include <gtest/gtest.h>

#include <helpers/TimeSyncAuth.h>
#include <Utils.h>
#include <ed_25519.h>
#include <stdio.h>
#include <string.h>

// Fixed test-only Ed25519 keypair used for deterministic vectors. This is not
// production signing material.
static const uint8_t TEST_PRIVATE_KEY[64] = {
  0x70, 0x65, 0xe1, 0x8f, 0xd9, 0xfa, 0xbb, 0x70,
  0xc1, 0xed, 0x90, 0xdc, 0xa1, 0x99, 0x07, 0xde,
  0x69, 0x8c, 0x88, 0xb7, 0x09, 0xea, 0x14, 0x6e,
  0xaf, 0xd9, 0x3d, 0x9b, 0x83, 0x0c, 0x7b, 0x60,
  0xc4, 0x68, 0x11, 0x93, 0xc7, 0x9b, 0xbc, 0x39,
  0x94, 0x5b, 0xa8, 0x06, 0x41, 0x04, 0xbb, 0x61,
  0x8f, 0x8f, 0xd7, 0xa8, 0x4a, 0x0a, 0xf6, 0xf5,
  0x70, 0x33, 0xd6, 0xe8, 0xdd, 0xcd, 0x64, 0x71
};

static const uint8_t TEST_PUBLIC_KEY[32] = {
  0x1e, 0xc7, 0x71, 0x75, 0xb0, 0x91, 0x8e, 0xd2,
  0x06, 0xf9, 0xae, 0x04, 0xec, 0x13, 0x6d, 0x6d,
  0x5d, 0x43, 0x15, 0xbb, 0x26, 0x30, 0x54, 0x27,
  0xf6, 0x45, 0xb4, 0x92, 0xe9, 0x35, 0x0c, 0x10
};

static void bytesToLowerHex(char* dest, const uint8_t* src, size_t len) {
  // Keep vector generation independent from firmware helper visibility.
  static const char hex[] = "0123456789abcdef";

  // Encode each byte into two lowercase hexadecimal characters.
  for (size_t i = 0; i < len; i++) {
    dest[i * 2] = hex[src[i] >> 4];
    dest[i * 2 + 1] = hex[src[i] & 0x0F];
  }

  // Null terminate because the canonical builder treats the channel ID as C text.
  dest[len * 2] = 0;
}

static void signMessage(const mesh::GroupChannel& channel, const char* display_name,
                        uint32_t timestamp, uint16_t sequence, uint8_t signature[SIGNATURE_SIZE]) {
  // Convert the full configured channel identity to the canonical ASCII field.
  uint8_t channel_id[32];
  char channel_id_hex[sizeof(channel_id) * 2 + 1];
  char canonical[TIME_SYNC_CANONICAL_MAX_LEN];
  mesh::Utils::sha256(channel_id, sizeof(channel_id), channel.secret, 16);
  bytesToLowerHex(channel_id_hex, channel_id, sizeof(channel_id));

  // Build the exact byte string documented in docs/payloads.md.
  snprintf(canonical, sizeof(canonical), "%s\n%s\n%s\n%lu\n%u\n",
           TIME_SYNC_CANONICAL_DOMAIN,
           channel_id_hex,
           display_name,
           (unsigned long)timestamp,
           (unsigned int)sequence);

  // Sign with the fixed test key so expected vectors remain stable.
  ed25519_sign(signature, (const uint8_t*)canonical, strlen(canonical), TEST_PUBLIC_KEY, TEST_PRIVATE_KEY);
}

static TimeSyncConfigView makeConfig(mesh::GroupChannel& channel) {
  // Use the documented hashtag channel derivation for the shared test channel.
  TimeSyncAuth::configureHashtagChannel(channel, "#time");

  // Return the minimum complete authority configuration required by the parser.
  TimeSyncConfigView config;
  config.enabled = true;
  config.channel = channel;
  config.display_name = "TimeBot";
  config.public_key = TEST_PUBLIC_KEY;
  return config;
}

static size_t makeBinary(uint8_t* dest, const mesh::GroupChannel& channel, uint32_t timestamp,
                         uint16_t sequence, const char* payload_name, const char* signed_name) {
  // The caller can intentionally make payload_name and signed_name differ to
  // prove that the signature binds the display-name field.
  uint8_t signature[SIGNATURE_SIZE];
  size_t pos = 0;
  size_t name_len = strlen(payload_name);

  // Sign before serialising so mutation tests can alter individual wire bytes.
  signMessage(channel, signed_name, timestamp, sequence, signature);

  // Write the fixed binary marker at the start of the Tv1 data field.
  memcpy(&dest[pos], TIME_SYNC_BINARY_MARKER, 3); pos += 3;

  // Encode the timestamp in little-endian wire order.
  dest[pos++] = timestamp & 0xFF;
  dest[pos++] = (timestamp >> 8) & 0xFF;
  dest[pos++] = (timestamp >> 16) & 0xFF;
  dest[pos++] = (timestamp >> 24) & 0xFF;

  // Encode the 16-bit monotonic sequence in little-endian wire order.
  dest[pos++] = sequence & 0xFF;
  dest[pos++] = (sequence >> 8) & 0xFF;

  // Store the exact display-name bytes that the receiver must compare.
  dest[pos++] = (uint8_t)name_len;
  memcpy(&dest[pos], payload_name, name_len); pos += name_len;

  // Append the raw Ed25519 signature; binary transport avoids Base64 overhead.
  memcpy(&dest[pos], signature, sizeof(signature)); pos += sizeof(signature);

  // Return the data-field length for parseAndVerifyBinary().
  return pos;
}

TEST(TimeSyncAuth, RejectsInvalidDisplayNames) {
  // Empty and newline-bearing display names cannot be configured safely.
  EXPECT_FALSE(TimeSyncAuth::isValidDisplayName(""));
  EXPECT_FALSE(TimeSyncAuth::isValidDisplayName("Time\nBot"));
  EXPECT_TRUE(TimeSyncAuth::isValidDisplayName("TimeBot"));
}

TEST(TimeSyncAuth, RejectsWildcardLikePublicKeys) {
  // All-zero keys would act like a placeholder or wildcard and are refused.
  uint8_t key[PUB_KEY_SIZE];
  memset(key, 0, sizeof(key));
  EXPECT_FALSE(TimeSyncAuth::isValidPublicKey(key));

  // All-0xFF keys are also refused for the same wildcard-like reason.
  memset(key, 0xFF, sizeof(key));
  EXPECT_FALSE(TimeSyncAuth::isValidPublicKey(key));

  // A real fixed test public key is accepted.
  EXPECT_TRUE(TimeSyncAuth::isValidPublicKey(TEST_PUBLIC_KEY));
}

TEST(TimeSyncAuth, AcceptsValidBinaryMessage) {
  // A payload signed by the configured authority should authenticate cleanly.
  mesh::GroupChannel channel;
  TimeSyncConfigView config = makeConfig(channel);
  uint8_t data[96];
  size_t data_len = makeBinary(data, channel, 1783862400UL, 12345, "TimeBot", "TimeBot");

  TimeSyncMessage msg;
  EXPECT_EQ(TIME_SYNC_OK, TimeSyncAuth::parseAndVerifyBinary(config, data, data_len, msg));
  EXPECT_EQ(1783862400UL, msg.timestamp);
  EXPECT_EQ(12345, msg.sequence);
}

TEST(TimeSyncAuth, RejectsWrongDisplayNameAndPrefix) {
  // Display-name matching is exact; a shorter prefix must not pass.
  mesh::GroupChannel channel;
  TimeSyncConfigView config = makeConfig(channel);
  uint8_t data[96];

  TimeSyncMessage msg;
  size_t data_len = makeBinary(data, channel, 1783862400UL, 12345, "Time", "Time");
  EXPECT_EQ(TIME_SYNC_DISPLAY_NAME_MISMATCH,
            TimeSyncAuth::parseAndVerifyBinary(config, data, data_len, msg));

  // A longer name with the configured name as a prefix must not pass either.
  data_len = makeBinary(data, channel, 1783862400UL, 12345, "TimeBotX", "TimeBotX");
  EXPECT_EQ(TIME_SYNC_DISPLAY_NAME_MISMATCH,
            TimeSyncAuth::parseAndVerifyBinary(config, data, data_len, msg));
}

TEST(TimeSyncAuth, RejectsChangedSignedField) {
  // Mutating a signed field after signing must invalidate the signature.
  mesh::GroupChannel channel;
  TimeSyncConfigView config = makeConfig(channel);
  uint8_t data[96];
  size_t data_len = makeBinary(data, channel, 1783862400UL, 12345, "TimeBot", "TimeBot");
  data[3] ^= 0x01;

  TimeSyncMessage msg;
  EXPECT_EQ(TIME_SYNC_SIGNATURE_INVALID, TimeSyncAuth::parseAndVerifyBinary(config, data, data_len, msg));
}

TEST(TimeSyncAuth, RejectsMalformedBinaryPayloads) {
  // Malformed payload tests cover truncation, wrong marker and bad length.
  mesh::GroupChannel channel;
  TimeSyncConfigView config = makeConfig(channel);
  uint8_t data[96];
  size_t data_len = makeBinary(data, channel, 1783862400UL, 12345, "TimeBot", "TimeBot");
  TimeSyncMessage msg;

  EXPECT_EQ(TIME_SYNC_MALFORMED, TimeSyncAuth::parseAndVerifyBinary(config, data, 2, msg));

  data[0] = 'X';
  EXPECT_EQ(TIME_SYNC_MALFORMED, TimeSyncAuth::parseAndVerifyBinary(config, data, data_len, msg));

  data[0] = 'T';
  EXPECT_EQ(TIME_SYNC_MALFORMED, TimeSyncAuth::parseAndVerifyBinary(config, data, data_len - 1, msg));
}

TEST(TimeSyncAuth, RejectsForgedSignature) {
  // Flipping a signature byte proves matching structure alone is insufficient.
  mesh::GroupChannel channel;
  TimeSyncConfigView config = makeConfig(channel);
  uint8_t data[96];
  size_t data_len = makeBinary(data, channel, 1783862400UL, 12345, "TimeBot", "TimeBot");
  data[data_len - 1] ^= 0x01;

  TimeSyncMessage msg;
  EXPECT_EQ(TIME_SYNC_SIGNATURE_INVALID, TimeSyncAuth::parseAndVerifyBinary(config, data, data_len, msg));
}

TEST(TimeSyncAuth, RejectsSignatureFromAnotherDisplayName) {
  // The display name is included in the canonical signed bytes, so a signature
  // over a different name cannot authenticate the configured sender.
  mesh::GroupChannel channel;
  TimeSyncConfigView config = makeConfig(channel);
  uint8_t data[96];
  size_t data_len = makeBinary(data, channel, 1783862400UL, 12345, "TimeBot", "OtherBot");

  TimeSyncMessage msg;
  EXPECT_EQ(TIME_SYNC_SIGNATURE_INVALID, TimeSyncAuth::parseAndVerifyBinary(config, data, data_len, msg));
}

TEST(TimeSyncAuth, RejectsSignatureFromAnotherChannel) {
  // The signature is scoped to the configured channel identity, not only the
  // display name, timestamp and sequence.
  mesh::GroupChannel configured_channel;
  TimeSyncConfigView config = makeConfig(configured_channel);

  // Sign the wire fields for a different hashtag channel.
  mesh::GroupChannel other_channel;
  TimeSyncAuth::configureHashtagChannel(other_channel, "#other-time");
  uint8_t data[96];
  size_t data_len = makeBinary(data, other_channel, 1783862400UL, 12345, "TimeBot", "TimeBot");

  TimeSyncMessage msg;
  EXPECT_EQ(TIME_SYNC_SIGNATURE_INVALID, TimeSyncAuth::parseAndVerifyBinary(config, data, data_len, msg));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
