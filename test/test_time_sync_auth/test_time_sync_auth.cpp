#include <gtest/gtest.h>

#include <helpers/TimeSyncAuth.h>
#include <helpers/TimeSyncConsumer.h>
#include <helpers/TimeSyncPrefsLayout.h>
#include <helpers/TxtDataHelpers.h>
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

static const uint8_t OTHER_PUBLIC_KEY[32] = {
  0x7a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71,
  0x82, 0x93, 0xa4, 0xb5, 0xc6, 0xd7, 0xe8, 0xf9,
  0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
  0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f
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

static size_t makeGroupDatagram(uint8_t* dest, const uint8_t* payload, size_t payload_len,
                                uint16_t data_type = TIME_SYNC_BINARY_DATA_TYPE) {
  // Prefix the application payload exactly as BaseChatMesh::sendGroupData()
  // does before Mesh encrypts the group datagram.
  dest[0] = (uint8_t)(data_type & 0xFF);
  dest[1] = (uint8_t)(data_type >> 8);
  dest[2] = (uint8_t)payload_len;
  memcpy(&dest[3], payload, payload_len);
  return 3 + payload_len;
}

static void resetConsumer(TimeSyncStats& stats, TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES],
                          uint8_t& last_source, bool& clock_accepted) {
  memset(&stats, 0, sizeof(stats));
  TimeSyncConsumer::resetReplay(replay, last_source, clock_accepted);
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
  EXPECT_EQ(0, msg.source_index);
}

TEST(TimeSyncAuth, AcceptsConfiguredSecondarySource) {
  // Multi-source parsing should return the configured slot that matched the
  // display name and verified signature.
  mesh::GroupChannel channel;
  TimeSyncAuth::configureHashtagChannel(channel, "#time");
  TimeSyncAuthorityConfig sources[2];
  sources[0].slot_index = 0;
  sources[0].display_name = "OtherBot";
  sources[0].public_key = OTHER_PUBLIC_KEY;
  sources[1].slot_index = 1;
  sources[1].display_name = "TimeBot";
  sources[1].public_key = TEST_PUBLIC_KEY;

  TimeSyncConfigView config;
  config.enabled = true;
  config.channel = channel;
  config.source_count = 2;
  config.sources = sources;
  config.display_name = sources[0].display_name;
  config.public_key = sources[0].public_key;

  uint8_t data[96];
  size_t data_len = makeBinary(data, channel, 1783862400UL, 12345, "TimeBot", "TimeBot");

  TimeSyncMessage msg;
  EXPECT_EQ(TIME_SYNC_OK, TimeSyncAuth::parseAndVerifyBinaryMulti(config, data, data_len, msg));
  EXPECT_EQ(1, msg.source_index);
}

TEST(TimeSyncAuth, RejectsUnknownSourceDisplayName) {
  // Unknown names should be counted once as a name mismatch, not tried as a
  // wildcard against every configured key.
  mesh::GroupChannel channel;
  TimeSyncAuth::configureHashtagChannel(channel, "#time");
  TimeSyncAuthorityConfig sources[1];
  sources[0].slot_index = 0;
  sources[0].display_name = "TimeBot";
  sources[0].public_key = TEST_PUBLIC_KEY;

  TimeSyncConfigView config;
  config.enabled = true;
  config.channel = channel;
  config.source_count = 1;
  config.sources = sources;
  config.display_name = sources[0].display_name;
  config.public_key = sources[0].public_key;

  uint8_t data[96];
  size_t data_len = makeBinary(data, channel, 1783862400UL, 12345, "UnknownBot", "UnknownBot");

  TimeSyncMessage msg;
  EXPECT_EQ(TIME_SYNC_DISPLAY_NAME_MISMATCH,
            TimeSyncAuth::parseAndVerifyBinaryMulti(config, data, data_len, msg));
}

TEST(TimeSyncAuth, RejectsKnownSourceSignedByWrongKey) {
  // Matching a configured display name is not enough; the matching source's
  // pinned public key must verify the signature.
  mesh::GroupChannel channel;
  TimeSyncAuth::configureHashtagChannel(channel, "#time");
  TimeSyncAuthorityConfig sources[1];
  sources[0].slot_index = 1;
  sources[0].display_name = "TimeBot";
  sources[0].public_key = OTHER_PUBLIC_KEY;

  TimeSyncConfigView config;
  config.enabled = true;
  config.channel = channel;
  config.source_count = 1;
  config.sources = sources;
  config.display_name = sources[0].display_name;
  config.public_key = sources[0].public_key;

  uint8_t data[96];
  size_t data_len = makeBinary(data, channel, 1783862400UL, 12345, "TimeBot", "TimeBot");

  TimeSyncMessage msg;
  EXPECT_EQ(TIME_SYNC_SIGNATURE_INVALID,
            TimeSyncAuth::parseAndVerifyBinaryMulti(config, data, data_len, msg));
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

TEST(TimeSyncCli, ParsesSourceSlotCommandKeys) {
  // The repeater CLI accepts get/set keys of the form source.<slot>.<field>.
  TimeSyncSourceSettingKey parsed;

  EXPECT_TRUE(TimeSyncAuth::parseSourceSettingKey("source.0", parsed));
  EXPECT_EQ(0, parsed.source_index);
  EXPECT_STREQ("", parsed.field);

  EXPECT_TRUE(TimeSyncAuth::parseSourceSettingKey("source.1.display_name", parsed));
  EXPECT_EQ(1, parsed.source_index);
  EXPECT_STREQ("display_name", parsed.field);

  EXPECT_TRUE(TimeSyncAuth::parseSourceSettingKey("source.3.public_key", parsed));
  EXPECT_EQ(3, parsed.source_index);
  EXPECT_STREQ("public_key", parsed.field);

  EXPECT_TRUE(TimeSyncAuth::parseSourceSettingKey("source.2.clear", parsed));
  EXPECT_EQ(2, parsed.source_index);
  EXPECT_STREQ("clear", parsed.field);

  EXPECT_FALSE(TimeSyncAuth::parseSourceSettingKey("source.4.display_name", parsed));
  EXPECT_FALSE(TimeSyncAuth::parseSourceSettingKey("source.10.display_name", parsed));
  EXPECT_FALSE(TimeSyncAuth::parseSourceSettingKey("source.x.public_key", parsed));
  EXPECT_FALSE(TimeSyncAuth::parseSourceSettingKey("sources", parsed));
}

TEST(TimeSyncCli, AppliesSourceDisplayNameCommand) {
  char display_name[32];
  uint8_t public_key[PUB_KEY_SIZE];
  strcpy(display_name, "OldName");
  memcpy(public_key, OTHER_PUBLIC_KEY, sizeof(public_key));

  EXPECT_EQ(TIME_SYNC_SOURCE_SETTING_OK,
            TimeSyncAuth::applySourceSetting(display_name, sizeof(display_name), public_key,
                                             "display_name", "TimeBot"));
  EXPECT_STREQ("TimeBot", display_name);
  EXPECT_EQ(0, memcmp(public_key, OTHER_PUBLIC_KEY, sizeof(public_key)));
}

TEST(TimeSyncCli, RejectsInvalidSourceDisplayNameCommandWithoutMutation) {
  char display_name[32];
  uint8_t public_key[PUB_KEY_SIZE];
  strcpy(display_name, "OldName");
  memcpy(public_key, OTHER_PUBLIC_KEY, sizeof(public_key));

  EXPECT_EQ(TIME_SYNC_SOURCE_SETTING_INVALID_DISPLAY_NAME,
            TimeSyncAuth::applySourceSetting(display_name, sizeof(display_name), public_key,
                                             "display_name", "Bad\nName"));
  EXPECT_STREQ("OldName", display_name);
  EXPECT_EQ(0, memcmp(public_key, OTHER_PUBLIC_KEY, sizeof(public_key)));
}

TEST(TimeSyncCli, AppliesSourcePublicKeyCommand) {
  char display_name[32];
  uint8_t public_key[PUB_KEY_SIZE];
  char public_key_hex[PUB_KEY_SIZE * 2 + 1];
  strcpy(display_name, "TimeBot");
  memset(public_key, 0xAA, sizeof(public_key));
  bytesToLowerHex(public_key_hex, TEST_PUBLIC_KEY, sizeof(TEST_PUBLIC_KEY));

  EXPECT_EQ(TIME_SYNC_SOURCE_SETTING_OK,
            TimeSyncAuth::applySourceSetting(display_name, sizeof(display_name), public_key,
                                             "public_key", public_key_hex));
  EXPECT_STREQ("TimeBot", display_name);
  EXPECT_EQ(0, memcmp(public_key, TEST_PUBLIC_KEY, sizeof(public_key)));
}

TEST(TimeSyncCli, RejectsInvalidSourcePublicKeyCommandWithoutMutation) {
  char display_name[32];
  uint8_t public_key[PUB_KEY_SIZE];
  strcpy(display_name, "TimeBot");
  memcpy(public_key, OTHER_PUBLIC_KEY, sizeof(public_key));

  EXPECT_EQ(TIME_SYNC_SOURCE_SETTING_INVALID_PUBLIC_KEY,
            TimeSyncAuth::applySourceSetting(display_name, sizeof(display_name), public_key,
                                             "public_key", "00"));
  EXPECT_STREQ("TimeBot", display_name);
  EXPECT_EQ(0, memcmp(public_key, OTHER_PUBLIC_KEY, sizeof(public_key)));
}

TEST(TimeSyncCli, AppliesSourceClearCommand) {
  char display_name[32];
  uint8_t public_key[PUB_KEY_SIZE];
  strcpy(display_name, "TimeBot");
  memcpy(public_key, TEST_PUBLIC_KEY, sizeof(public_key));

  EXPECT_EQ(TIME_SYNC_SOURCE_SETTING_OK,
            TimeSyncAuth::applySourceSetting(display_name, sizeof(display_name), public_key,
                                             "clear", "yes"));
  EXPECT_EQ(0, display_name[0]);
  uint8_t zero_key[PUB_KEY_SIZE];
  memset(zero_key, 0, sizeof(zero_key));
  EXPECT_EQ(0, memcmp(public_key, zero_key, sizeof(public_key)));
}

TEST(TimeSyncCli, RejectsSourceClearWithoutYes) {
  char display_name[32];
  uint8_t public_key[PUB_KEY_SIZE];
  strcpy(display_name, "TimeBot");
  memcpy(public_key, TEST_PUBLIC_KEY, sizeof(public_key));

  EXPECT_EQ(TIME_SYNC_SOURCE_SETTING_CLEAR_REQUIRES_YES,
            TimeSyncAuth::applySourceSetting(display_name, sizeof(display_name), public_key,
                                             "clear", "no"));
  EXPECT_STREQ("TimeBot", display_name);
  EXPECT_EQ(0, memcmp(public_key, TEST_PUBLIC_KEY, sizeof(public_key)));
}

TEST(TimeSyncCli, RejectsUnknownSourceSettingCommand) {
  char display_name[32];
  uint8_t public_key[PUB_KEY_SIZE];
  strcpy(display_name, "TimeBot");
  memcpy(public_key, TEST_PUBLIC_KEY, sizeof(public_key));

  EXPECT_EQ(TIME_SYNC_SOURCE_SETTING_UNKNOWN_FIELD,
            TimeSyncAuth::applySourceSetting(display_name, sizeof(display_name), public_key,
                                             "unknown", "value"));
  EXPECT_STREQ("TimeBot", display_name);
  EXPECT_EQ(0, memcmp(public_key, TEST_PUBLIC_KEY, sizeof(public_key)));
}

TEST(TimeSyncPersistence, KeepsAdditionalSourcesAfterLegacyFields) {
  // Slot 0 remains the original single-source preference pair, while slots
  // 1..3 are appended after the legacy time-sync fields for older pref files.
  EXPECT_EQ(429, TIME_SYNC_PREFS_EXTRA_DISPLAY_NAMES_OFFSET);
  EXPECT_EQ(525, TIME_SYNC_PREFS_EXTRA_PUBLIC_KEYS_OFFSET);
  EXPECT_EQ(621, TIME_SYNC_PREFS_NEXT_OFFSET);

  EXPECT_EQ(TIME_SYNC_PREFS_EXTRA_DISPLAY_NAMES_OFFSET,
            TIME_SYNC_PREFS_MAX_FORWARD_STEP_OFFSET + (int)sizeof(uint32_t));
  EXPECT_EQ(TIME_SYNC_PREFS_EXTRA_PUBLIC_KEYS_OFFSET,
            TIME_SYNC_PREFS_EXTRA_DISPLAY_NAMES_OFFSET + TIME_SYNC_PREFS_EXTRA_DISPLAY_NAMES_SIZE);
  EXPECT_EQ(TIME_SYNC_PREFS_NEXT_OFFSET,
            TIME_SYNC_PREFS_EXTRA_PUBLIC_KEYS_OFFSET + TIME_SYNC_PREFS_EXTRA_PUBLIC_KEYS_SIZE);
  EXPECT_EQ(TIME_SYNC_MAX_SOURCES - 1, TIME_SYNC_PREFS_EXTRA_SOURCE_COUNT);
  EXPECT_EQ(32, TIME_SYNC_PREFS_DISPLAY_NAME_SIZE);
}

TEST(TimeSyncConsumer, EnforcesClockRange) {
  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);
  uint32_t new_time = 0;

  EXPECT_FALSE(TimeSyncConsumer::applyClock(stats, clock_accepted, TIME_SYNC_MIN_UNIX_TIME,
                                           TIME_SYNC_MIN_UNIX_TIME - 1, TIME_SYNC_DEFAULT_MAX_FORWARD_STEP,
                                           new_time));
  EXPECT_FALSE(TimeSyncConsumer::applyClock(stats, clock_accepted, TIME_SYNC_MIN_UNIX_TIME,
                                           TIME_SYNC_MAX_UNIX_TIME + 1UL, TIME_SYNC_DEFAULT_MAX_FORWARD_STEP,
                                           new_time));
  EXPECT_EQ(2UL, stats.stale_timestamp);
  EXPECT_EQ(0UL, stats.clock_updates);
}

TEST(TimeSyncConsumer, AllowsInitialFallbackException) {
  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);
  uint32_t new_time = 0;

  EXPECT_TRUE(TimeSyncConsumer::applyClock(stats, clock_accepted, TIME_SYNC_MIN_UNIX_TIME,
                                          TIME_SYNC_MIN_UNIX_TIME + 90000UL,
                                          TIME_SYNC_DEFAULT_MAX_FORWARD_STEP, new_time));
  EXPECT_EQ(TIME_SYNC_MIN_UNIX_TIME + 90000UL, new_time);
  EXPECT_TRUE(clock_accepted);
  EXPECT_EQ(1UL, stats.clock_updates);
}

TEST(TimeSyncConsumer, EnforcesMaximumForwardStepAfterInitialisation) {
  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);
  clock_accepted = true;
  uint32_t new_time = 0;

  EXPECT_FALSE(TimeSyncConsumer::applyClock(stats, clock_accepted, 1783862400UL, 1783869601UL,
                                           3600UL, new_time));
  EXPECT_EQ(1UL, stats.excessive_forward_step);
  EXPECT_EQ(0UL, stats.clock_updates);
}

TEST(TimeSyncConsumer, AcceptsFutureClockRecoveryWithinStep) {
  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);
  clock_accepted = true;
  uint32_t new_time = 0;

  EXPECT_TRUE(TimeSyncConsumer::applyClock(stats, clock_accepted, 1783862400UL, 1783862700UL,
                                          3600UL, new_time));
  EXPECT_EQ(1783862700UL, new_time);
  EXPECT_EQ(1UL, stats.clock_updates);
}

TEST(TimeSyncConsumer, ForwardOnlyClockLimitsReplayAfterReboot) {
  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);

  TimeSyncMessage msg;
  msg.timestamp = 1783862400UL;
  msg.sequence = 7;
  msg.source_index = 0;
  uint32_t new_time = 0;

  EXPECT_FALSE(TimeSyncConsumer::handleResult(stats, replay, last_source, clock_accepted, TIME_SYNC_OK,
                                             msg, 1783862400UL, 3600UL, new_time));
  EXPECT_EQ(1UL, stats.stale_timestamp);
  EXPECT_EQ(0UL, stats.accepted);
}

TEST(TimeSyncConsumer, HandlesSequenceWrapOnNewerTimestamp) {
  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);
  clock_accepted = true;

  replay[0].accepted_this_boot = true;
  replay[0].last_timestamp = 1783862400UL;
  replay[0].last_sequence = 65535;
  TimeSyncMessage msg;
  msg.timestamp = 1783862401UL;
  msg.sequence = 0;
  msg.source_index = 0;
  uint32_t new_time = 0;

  EXPECT_TRUE(TimeSyncConsumer::handleResult(stats, replay, last_source, clock_accepted, TIME_SYNC_OK,
                                            msg, 1783862400UL, 3600UL, new_time));
  EXPECT_EQ(1783862401UL, new_time);
  EXPECT_EQ(0, replay[0].last_sequence);
  EXPECT_EQ(1UL, stats.accepted);
}

TEST(TimeSyncConsumer, RejectsEqualTimestampReplayAcrossSequenceWrap) {
  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);
  clock_accepted = true;

  replay[0].accepted_this_boot = true;
  replay[0].last_timestamp = 1783862400UL;
  replay[0].last_sequence = 65535;
  TimeSyncMessage msg;
  msg.timestamp = 1783862400UL;
  msg.sequence = 0;
  msg.source_index = 0;
  uint32_t new_time = 0;

  EXPECT_FALSE(TimeSyncConsumer::handleResult(stats, replay, last_source, clock_accepted, TIME_SYNC_OK,
                                             msg, 1783862400UL, 3600UL, new_time));
  EXPECT_EQ(1UL, stats.stale_timestamp);
}

TEST(TimeSyncConsumer, LooksUpConfiguredChannelOnly) {
  mesh::GroupChannel configured;
  TimeSyncConfigView config = makeConfig(configured);
  TimeSyncAuthorityConfig source;
  source.slot_index = 0;
  source.display_name = config.display_name;
  source.public_key = config.public_key;
  config.source_count = 1;
  config.sources = &source;

  mesh::GroupChannel out[1];
  EXPECT_EQ(1, TimeSyncConsumer::searchChannelByHash(config, configured.hash, out, 1));
  EXPECT_EQ(0, memcmp(configured.hash, out[0].hash, PATH_HASH_SIZE));

  uint8_t wrong_hash[PATH_HASH_SIZE];
  memset(wrong_hash, configured.hash[0] ^ 0xFF, sizeof(wrong_hash));
  EXPECT_EQ(0, TimeSyncConsumer::searchChannelByHash(config, wrong_hash, out, 1));
}

TEST(TimeSyncConsumer, CountsParserOutcomesAccurately) {
  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);
  TimeSyncMessage msg;
  msg.timestamp = 1783862400UL;
  msg.sequence = 1;
  msg.source_index = 0;
  uint32_t new_time = 0;

  EXPECT_FALSE(TimeSyncConsumer::handleResult(stats, replay, last_source, clock_accepted,
                                             TIME_SYNC_DISPLAY_NAME_MISMATCH, msg, 1783862300UL,
                                             3600UL, new_time));
  EXPECT_FALSE(TimeSyncConsumer::handleResult(stats, replay, last_source, clock_accepted,
                                             TIME_SYNC_SIGNATURE_INVALID, msg, 1783862300UL,
                                             3600UL, new_time));
  EXPECT_FALSE(TimeSyncConsumer::handleResult(stats, replay, last_source, clock_accepted,
                                             TIME_SYNC_MALFORMED, msg, 1783862300UL, 3600UL,
                                             new_time));

  EXPECT_EQ(1UL, stats.display_name_mismatch);
  EXPECT_EQ(1UL, stats.signature_invalid);
  EXPECT_EQ(1UL, stats.malformed);
}

TEST(TimeSyncConsumer, AppliesFullPacketReceptionToClock) {
  mesh::GroupChannel channel;
  TimeSyncConfigView config = makeConfig(channel);
  TimeSyncAuthorityConfig source;
  source.slot_index = 0;
  source.display_name = config.display_name;
  source.public_key = config.public_key;
  config.source_count = 1;
  config.sources = &source;

  uint8_t payload[96];
  size_t payload_len = makeBinary(payload, channel, 1783862400UL, 12345, "TimeBot", "TimeBot");
  uint8_t datagram[128];
  size_t datagram_len = makeGroupDatagram(datagram, payload, payload_len);

  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);
  uint32_t new_time = 0;

  EXPECT_TRUE(TimeSyncConsumer::consumeGroupData(stats, replay, last_source, clock_accepted, config,
                                                PAYLOAD_TYPE_GRP_DATA, datagram, datagram_len,
                                                TIME_SYNC_MIN_UNIX_TIME, 3600UL, new_time));
  EXPECT_EQ(1783862400UL, new_time);
  EXPECT_EQ(1UL, stats.received);
  EXPECT_EQ(1UL, stats.accepted);
  EXPECT_EQ(1UL, stats.clock_updates);
  EXPECT_EQ(0, last_source);
}

TEST(TimeSyncConsumer, IgnoresNonTimeSyncDatagramsOnConfiguredChannel) {
  mesh::GroupChannel channel;
  TimeSyncConfigView config = makeConfig(channel);
  TimeSyncAuthorityConfig source;
  source.slot_index = 0;
  source.display_name = config.display_name;
  source.public_key = config.public_key;
  config.source_count = 1;
  config.sources = &source;

  uint8_t payload[] = {0xAA, 0xBB, 0xCC};
  uint8_t datagram[16];
  size_t datagram_len = makeGroupDatagram(datagram, payload, sizeof(payload), DATA_TYPE_DEV);

  TimeSyncStats stats;
  TimeSyncReplayState replay[TIME_SYNC_MAX_SOURCES];
  uint8_t last_source;
  bool clock_accepted;
  resetConsumer(stats, replay, last_source, clock_accepted);
  uint32_t new_time = 0;

  EXPECT_FALSE(TimeSyncConsumer::consumeGroupData(stats, replay, last_source, clock_accepted, config,
                                                 PAYLOAD_TYPE_GRP_DATA, datagram, datagram_len,
                                                 1783862300UL, 3600UL, new_time));
  EXPECT_EQ(0UL, stats.received);
  EXPECT_EQ(0UL, stats.malformed);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
