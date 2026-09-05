#include <gtest/gtest.h>

#include <ArduinoJson.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "helpers/MQTTPayloadBuilder.h"

namespace {

constexpr const char* kTimestamp = "2026-07-18T12:34:56.123456+00:00";

struct RejectAllJsonAllocations : ArduinoJson::Allocator {
  void* allocate(size_t) override { return nullptr; }
  void deallocate(void*) override {}
  void* reallocate(void*, size_t) override { return nullptr; }
};

static int buildMinimalStatus(JsonDocument& scratch, char* buffer, size_t buffer_size) {
  return MQTTPayloadBuilder::buildStatusMessage(
      scratch, "DEN Repeater", "0123456789ABCDEF", "Heltec V3", "v1.16.0",
      "915.000000,62.5,7,5", "MeshCore", "online", kTimestamp,
      buffer, buffer_size);
}

static int buildRepresentativePacket(JsonDocument& scratch, const char* direction,
                                     float score, const uint8_t* path, int path_hops,
                                     int path_hash_size, const char* raw,
                                     char* buffer, size_t buffer_size) {
  return MQTTPayloadBuilder::buildPacketMessage(
      scratch, "DEN Repeater", "0123456789ABCDEF", kTimestamp, direction,
      "12:34:56", "18/07/2026", 42, 4, "D", 20, raw,
      10.26f, -87, score, "89ABCDEF01234567", path, path_hops,
      path_hash_size, 64, buffer, buffer_size);
}

TEST(MQTTPayloadBuilder, MinimalStatusHasExactRequiredContract) {
  JsonDocument scratch;
  char buffer[768];
  int len = buildMinimalStatus(scratch, buffer, sizeof(buffer));

  ASSERT_GT(len, 0);
  EXPECT_EQ(static_cast<size_t>(len), strlen(buffer));
  EXPECT_STREQ(
      "{\"status\":\"online\",\"timestamp\":\"2026-07-18T12:34:56.123456+00:00\","
      "\"origin\":\"DEN Repeater\",\"origin_id\":\"0123456789ABCDEF\","
      "\"model\":\"Heltec V3\",\"firmware_version\":\"v1.16.0\","
      "\"radio\":\"915.000000,62.5,7,5\",\"client_version\":\"MeshCore\"}",
      buffer);

  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_FALSE(parsed["repeat"].is<JsonVariant>());
  EXPECT_FALSE(parsed["stats"].is<JsonVariant>());
}

TEST(MQTTPayloadBuilder, StatusIncludesRepeatAndEveryRequestedStatistic) {
  JsonDocument scratch;
  char buffer[1024];
  int len = MQTTPayloadBuilder::buildStatusMessage(
      scratch, "node", "id", "model", "firmware", "radio", "client", "online",
      kTimestamp, buffer, sizeof(buffer), 4200, 86400, 3, 6, -112,
      11, 22, 4, 180864, 31, 47, "on");

  ASSERT_GT(len, 0);
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_STREQ("on", parsed["repeat"].as<const char*>());
  JsonObject stats = parsed["stats"].as<JsonObject>();
  ASSERT_FALSE(stats.isNull());
  EXPECT_EQ(4200, stats["battery_mv"].as<int>());
  EXPECT_EQ(86400, stats["uptime_secs"].as<int>());
  EXPECT_EQ(3, stats["errors"].as<int>());
  EXPECT_EQ(6, stats["queue_len"].as<int>());
  EXPECT_EQ(-112, stats["noise_floor"].as<int>());
  EXPECT_EQ(11, stats["tx_air_secs"].as<int>());
  EXPECT_EQ(22, stats["rx_air_secs"].as<int>());
  EXPECT_EQ(4, stats["recv_errors"].as<int>());
  EXPECT_EQ(180864, stats["internal_heap"].as<int>());
  EXPECT_EQ(31, stats["packets_sent"].as<int>());
  EXPECT_EQ(47, stats["packets_received"].as<int>());
}

TEST(MQTTPayloadBuilder, StatusOmissionSentinelsRemainOmitted) {
  JsonDocument scratch;
  char buffer[768];
  int len = MQTTPayloadBuilder::buildStatusMessage(
      scratch, "node", "id", "model", "firmware", "radio", "client", "online",
      kTimestamp, buffer, sizeof(buffer), -1, -1, -1, -1, -999,
      -1, -1, -1, -1, -1, -1, nullptr);

  ASSERT_GT(len, 0);
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_FALSE(parsed["stats"].is<JsonVariant>());
  EXPECT_FALSE(parsed["repeat"].is<JsonVariant>());
}

TEST(MQTTPayloadBuilder, StringsAreEscapedAndRoundTrip) {
  const char* origin = "node \"north\"\\rack\nline";
  const char* model = "Heltec\tV3";
  JsonDocument scratch;
  char buffer[1024];
  int len = MQTTPayloadBuilder::buildStatusMessage(
      scratch, origin, "id", model, "v1", "radio", "client", "online",
      kTimestamp, buffer, sizeof(buffer));

  ASSERT_GT(len, 0);
  EXPECT_NE(std::string::npos, std::string(buffer).find("\\\"north\\\""));
  EXPECT_NE(std::string::npos, std::string(buffer).find("\\\\rack\\nline"));
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_STREQ(origin, parsed["origin"].as<const char*>());
  EXPECT_STREQ(model, parsed["model"].as<const char*>());
}

TEST(MQTTPayloadBuilder, RxPacketIncludesMetricsScaledScoreAndPath) {
  const uint8_t path[] = {0xAA, 0xBB, 0x01, 0x2F};
  JsonDocument scratch;
  char buffer[2048];
  int len = buildRepresentativePacket(
      scratch, "rx", 0.125f, path, 2, 2, "A0B1C2D3", buffer, sizeof(buffer));

  ASSERT_GT(len, 0);
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_STREQ("PACKET", parsed["type"].as<const char*>());
  EXPECT_STREQ("rx", parsed["direction"].as<const char*>());
  EXPECT_STREQ("42", parsed["len"].as<const char*>());
  EXPECT_STREQ("4", parsed["packet_type"].as<const char*>());
  EXPECT_STREQ("20", parsed["payload_len"].as<const char*>());
  EXPECT_STREQ("10.3", parsed["SNR"].as<const char*>());
  EXPECT_STREQ("-87", parsed["RSSI"].as<const char*>());
  EXPECT_STREQ("125", parsed["score"].as<const char*>());
  JsonArray parsed_path = parsed["path"].as<JsonArray>();
  ASSERT_EQ(2U, parsed_path.size());
  EXPECT_STREQ("aabb", parsed_path[0].as<const char*>());
  EXPECT_STREQ("012f", parsed_path[1].as<const char*>());
}

TEST(MQTTPayloadBuilder, TxPacketOmitsReceiveOnlyMetricsAndAbsentPath) {
  JsonDocument scratch;
  char buffer[2048];
  int len = buildRepresentativePacket(
      scratch, "tx", 0.5f, nullptr, 0, 0, "A0B1", buffer, sizeof(buffer));

  ASSERT_GT(len, 0);
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_FALSE(parsed["SNR"].is<JsonVariant>());
  EXPECT_FALSE(parsed["RSSI"].is<JsonVariant>());
  EXPECT_FALSE(parsed["score"].is<JsonVariant>());
  EXPECT_FALSE(parsed["path"].is<JsonVariant>());
}

TEST(MQTTPayloadBuilder, RxPacketOmitsUnknownNanScore) {
  JsonDocument scratch;
  char buffer[2048];
  int len = buildRepresentativePacket(
      scratch, "rx", std::nanf(""), nullptr, 0, 0, "A0B1", buffer, sizeof(buffer));

  ASSERT_GT(len, 0);
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_TRUE(parsed["SNR"].is<const char*>());
  EXPECT_TRUE(parsed["RSSI"].is<const char*>());
  EXPECT_FALSE(parsed["score"].is<JsonVariant>());
}

TEST(MQTTPayloadBuilder, RawMessageHasExactContractAndEscapesData) {
  char buffer[512];
  JsonDocument doc;
  int len = MQTTPayloadBuilder::buildRawMessage(
      doc, "node \"A\"", "id\\1", kTimestamp, "AA\nBB", buffer, sizeof(buffer));

  ASSERT_GT(len, 0);
  EXPECT_EQ(static_cast<size_t>(len), strlen(buffer));
  EXPECT_STREQ(
      "{\"origin\":\"node \\\"A\\\"\",\"origin_id\":\"id\\\\1\","
      "\"timestamp\":\"2026-07-18T12:34:56.123456+00:00\","
      "\"type\":\"RAW\",\"data\":\"AA\\nBB\"}",
      buffer);
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_STREQ("AA\nBB", parsed["data"].as<const char*>());
}

TEST(MQTTPayloadBuilder, ExactOutputSizeSucceedsAndOneByteShortFailsCleanly) {
  JsonDocument scratch;
  char reference[768];
  int reference_len = buildMinimalStatus(scratch, reference, sizeof(reference));
  ASSERT_GT(reference_len, 0);

  std::vector<char> exact(static_cast<size_t>(reference_len) + 1);
  EXPECT_EQ(reference_len, buildMinimalStatus(scratch, exact.data(), exact.size()));
  EXPECT_STREQ(reference, exact.data());

  std::vector<char> short_buffer(static_cast<size_t>(reference_len), 'x');
  EXPECT_EQ(0, buildMinimalStatus(scratch, short_buffer.data(), short_buffer.size()));
  EXPECT_EQ('\0', short_buffer[0]);
  EXPECT_EQ(0, buildMinimalStatus(scratch, short_buffer.data(), 1));
  EXPECT_EQ('\0', short_buffer[0]);
  EXPECT_EQ(0, buildMinimalStatus(scratch, nullptr, exact.size()));
}

TEST(MQTTPayloadBuilder, MaximumRepresentativePacketAndRawPayloadsRemainValid) {
  uint8_t path[64];
  for (size_t i = 0; i < sizeof(path); ++i) path[i] = static_cast<uint8_t>(i);
  std::string raw(510, 'A');

  JsonDocument scratch;
  char packet_buffer[2048];
  int packet_len = buildRepresentativePacket(
      scratch, "rx", 1.0f, path, 16, 4, raw.c_str(),
      packet_buffer, sizeof(packet_buffer));
  ASSERT_GT(packet_len, 0);
  JsonDocument packet;
  ASSERT_FALSE(deserializeJson(packet, packet_buffer));
  EXPECT_EQ(510U, strlen(packet["raw"].as<const char*>()));
  JsonArray parsed_path = packet["path"].as<JsonArray>();
  ASSERT_EQ(16U, parsed_path.size());
  EXPECT_STREQ("00010203", parsed_path[0].as<const char*>());
  EXPECT_STREQ("3c3d3e3f", parsed_path[15].as<const char*>());

  char raw_buffer[1024];
  JsonDocument raw_doc;
  int raw_len = MQTTPayloadBuilder::buildRawMessage(
      raw_doc, "node", "0123456789ABCDEF", kTimestamp, raw.c_str(),
      raw_buffer, sizeof(raw_buffer));
  ASSERT_GT(raw_len, 0);
  JsonDocument parsed_raw;
  ASSERT_FALSE(deserializeJson(parsed_raw, raw_buffer));
  EXPECT_EQ(510U, strlen(parsed_raw["data"].as<const char*>()));
}

TEST(MQTTPayloadBuilder, NeighborsMessageRoundTripsSelfAndEntries) {
  MQTTPayloadBuilder::NeighborsMessageEntry neighbors[] = {
      {"0011223344556677", 9.75f, 42, "DEN,APRS", "active"},
      {"8899AABBCCDDEEFF", -3.5f, 3600, "", "stale"},
  };

  JsonDocument scratch;
  char buffer[1024];
  int len = MQTTPayloadBuilder::buildNeighborsMessage(
      scratch, "DEN Repeater", "0123456789ABCDEF", kTimestamp, "DEN,APRS", "*",
      neighbors, 2, buffer, sizeof(buffer), 5, 2, true);

  ASSERT_GT(len, 0);
  EXPECT_EQ(static_cast<size_t>(len), strlen(buffer));
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_STREQ("2026-07-18T12:34:56.123456+00:00", parsed["timestamp"].as<const char*>());
  EXPECT_STREQ("DEN Repeater", parsed["origin"].as<const char*>());
  EXPECT_STREQ("0123456789ABCDEF", parsed["origin_id"].as<const char*>());
  EXPECT_STREQ("DEN,APRS", parsed["self"]["scopes"].as<const char*>());
  EXPECT_STREQ("*", parsed["self"]["default_scope"].as<const char*>());
  EXPECT_EQ(5, parsed["total_neighbors"].as<int>());
  EXPECT_EQ(2, parsed["queried_neighbors"].as<int>());
  EXPECT_TRUE(parsed["truncated"].as<bool>());

  JsonArray arr = parsed["neighbors"].as<JsonArray>();
  ASSERT_EQ(2U, arr.size());
  EXPECT_STREQ("0011223344556677", arr[0]["pubkey"].as<const char*>());
  EXPECT_FLOAT_EQ(9.75f, arr[0]["snr"].as<float>());
  EXPECT_EQ(42U, arr[0]["heard_secs_ago"].as<uint32_t>());
  EXPECT_STREQ("DEN,APRS", arr[0]["scopes"].as<const char*>());
  EXPECT_STREQ("active", arr[0]["status"].as<const char*>());
  EXPECT_STREQ("8899AABBCCDDEEFF", arr[1]["pubkey"].as<const char*>());
  EXPECT_STREQ("", arr[1]["scopes"].as<const char*>());
  EXPECT_STREQ("stale", arr[1]["status"].as<const char*>());
}

TEST(MQTTPayloadBuilder, NeighborsMessageMeasurementsMatchCompletePayload) {
  MQTTPayloadBuilder::NeighborsMessageEntry neighbors[] = {
      {"0011223344556677", 9.75f, UINT32_MAX, "DEN,APRS", "responded"},
      {"8899AABBCCDDEEFF", -3.5f, UINT32_MAX, "", "timeout"},
  };

  size_t measured =
      MQTTPayloadBuilder::measureNeighborsMessageBase(
          "DEN Repeater", "0123456789ABCDEF", kTimestamp, "DEN,APRS", "*", 2)
      + MQTTPayloadBuilder::measureNeighborsMessageEntry(neighbors[0])
      + 1  // array comma
      + MQTTPayloadBuilder::measureNeighborsMessageEntry(neighbors[1]);

  JsonDocument scratch;
  char buffer[1024];
  int len = MQTTPayloadBuilder::buildNeighborsMessage(
      scratch, "DEN Repeater", "0123456789ABCDEF", kTimestamp, "DEN,APRS", "*",
      neighbors, 2, buffer, sizeof(buffer), 2, 2, false);

  ASSERT_GT(len, 0);
  EXPECT_EQ(measured, static_cast<size_t>(len));
}

TEST(MQTTPayloadBuilder, NeighborsMessageMeasuredPrefixStopsBeforeOverflow) {
  MQTTPayloadBuilder::NeighborsMessageEntry neighbors[20];
  static char keys[20][17];
  static char long_scopes[96];
  memset(long_scopes, 'S', sizeof(long_scopes) - 1);
  long_scopes[sizeof(long_scopes) - 1] = '\0';
  for (int i = 0; i < 20; i++) {
    snprintf(keys[i], sizeof(keys[i]), "%016X", i);
    neighbors[i] = {
        keys[i], static_cast<float>(i), UINT32_MAX,
        long_scopes, "responded"};
  }

  constexpr size_t kBufferSize = 512;
  size_t used = MQTTPayloadBuilder::measureNeighborsMessageBase(
      "node", "id", kTimestamp, "DEN", "*", 20);
  int published = 0;
  int queried = 0;
  while (queried < 20) {
    size_t added =
        MQTTPayloadBuilder::measureNeighborsMessageEntry(neighbors[queried])
        + (published > 0 ? 1U : 0U);
    queried++;
    if (used + added >= kBufferSize) break;
    used += added;
    published++;
  }

  ASSERT_GT(published, 0);
  ASSERT_LT(published, 20);
  ASSERT_EQ(published + 1, queried);  // only the non-fitting candidate was extra

  JsonDocument scratch;
  char buffer[kBufferSize];
  int len = MQTTPayloadBuilder::buildNeighborsMessage(
      scratch, "node", "id", kTimestamp, "DEN", "*",
      neighbors, published, buffer, sizeof(buffer),
      20, queried, true);

  ASSERT_GT(len, 0);
  EXPECT_LT(static_cast<size_t>(len), sizeof(buffer));
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_EQ(static_cast<size_t>(published),
            parsed["neighbors"].as<JsonArray>().size());
  EXPECT_EQ(20, parsed["total_neighbors"].as<int>());
  EXPECT_EQ(queried, parsed["queried_neighbors"].as<int>());
  EXPECT_TRUE(parsed["truncated"].as<bool>());
}

TEST(MQTTPayloadBuilder, NeighborsMessageFallbackMarksTruncated) {
  MQTTPayloadBuilder::NeighborsMessageEntry neighbors[] = {
      {"0011223344556677", 9.75f, 1, "DEN", "responded"},
      {"8899AABBCCDDEEFF", -3.5f, 2, "APRS", "responded"},
  };
  size_t first_only_size =
      MQTTPayloadBuilder::measureNeighborsMessageBase(
          "node", "id", kTimestamp, "DEN", "*", 2)
      + MQTTPayloadBuilder::measureNeighborsMessageEntry(neighbors[0]);
  std::vector<char> buffer(first_only_size + 1);

  JsonDocument scratch;
  int len = MQTTPayloadBuilder::buildNeighborsMessage(
      scratch, "node", "id", kTimestamp, "DEN", "*",
      neighbors, 2, buffer.data(), buffer.size(),
      2, 2, false);

  ASSERT_GT(len, 0);
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer.data()));
  EXPECT_EQ(1U, parsed["neighbors"].as<JsonArray>().size());
  EXPECT_TRUE(parsed["truncated"].as<bool>());
}

TEST(MQTTPayloadBuilder, NeighborsMessageFailsCleanlyOnAllocationFailure) {
  MQTTPayloadBuilder::NeighborsMessageEntry neighbor = {
      "0011223344556677", 9.75f, 1, "DEN", "responded"};
  RejectAllJsonAllocations allocator;
  JsonDocument scratch(&allocator);
  char buffer[512];

  EXPECT_EQ(0, MQTTPayloadBuilder::buildNeighborsMessage(
      scratch, "node", "id", kTimestamp, "DEN", "*",
      &neighbor, 1, buffer, sizeof(buffer),
      1, 1, false));
}

TEST(MQTTPayloadBuilder, NeighborsMessageHandlesEmptyTableAndNullScopes) {
  JsonDocument scratch;
  char buffer[256];
  int len = MQTTPayloadBuilder::buildNeighborsMessage(
      scratch, "node", "id", kTimestamp, nullptr, nullptr, nullptr, 0,
      buffer, sizeof(buffer));

  ASSERT_GT(len, 0);
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  EXPECT_STREQ("", parsed["self"]["scopes"].as<const char*>());
  EXPECT_STREQ("", parsed["self"]["default_scope"].as<const char*>());
  JsonArray arr = parsed["neighbors"].as<JsonArray>();
  ASSERT_TRUE(arr.isNull() == false);
  EXPECT_EQ(0U, arr.size());
}

TEST(MQTTPayloadBuilder, NeighborsMessageDropsTailWhenBufferFills) {
  // Twenty entries far exceed a tight buffer; the builder must emit a prefix
  // that still parses as complete JSON rather than truncating mid-document.
  MQTTPayloadBuilder::NeighborsMessageEntry neighbors[20];
  static char keys[20][17];
  for (int i = 0; i < 20; i++) {
    snprintf(keys[i], sizeof(keys[i]), "%016X", i);
    neighbors[i].pubkey_hex = keys[i];
    neighbors[i].snr = static_cast<float>(i);
    neighbors[i].heard_secs_ago = static_cast<uint32_t>(i) * 10U;
    neighbors[i].heard_unknown = false;
    neighbors[i].scopes = "DEN";
    neighbors[i].status = "active";
  }

  JsonDocument scratch;
  char buffer[512];
  int len = MQTTPayloadBuilder::buildNeighborsMessage(
      scratch, "node", "id", kTimestamp, "DEN", "*", neighbors, 20,
      buffer, sizeof(buffer));

  ASSERT_GT(len, 0);
  EXPECT_LT(static_cast<size_t>(len), sizeof(buffer));
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  JsonArray arr = parsed["neighbors"].as<JsonArray>();
  ASSERT_FALSE(arr.isNull());
  EXPECT_GT(arr.size(), 0U);
  EXPECT_LT(arr.size(), 20U);
  EXPECT_FALSE(parsed["truncated"].is<JsonVariant>());
  // Kept entries are the head of the input, in order.
  EXPECT_STREQ(keys[0], arr[0]["pubkey"].as<const char*>());
}

TEST(MQTTPayloadBuilder, NeighborsMessageRendersUnknownHeardAgeAsNull) {
  // A neighbour stamped before the clock was set has no usable age. The key
  // stays present and null so a consumer cannot read it as "heard just now".
  MQTTPayloadBuilder::NeighborsMessageEntry neighbors[2];
  neighbors[0] = {"0011223344556677", 9.75f, 42, "DEN", "responded"};
  neighbors[1] = {"8899AABBCCDDEEFF", -3.5f, 0, "DEN", "responded"};
  neighbors[1].heard_unknown = true;

  JsonDocument scratch;
  char buffer[1024];
  int len = MQTTPayloadBuilder::buildNeighborsMessage(
      scratch, "node", "id", kTimestamp, "DEN", "*", neighbors, 2,
      buffer, sizeof(buffer), 2, 2, false);

  ASSERT_GT(len, 0);
  JsonDocument parsed;
  ASSERT_FALSE(deserializeJson(parsed, buffer));
  JsonArray arr = parsed["neighbors"].as<JsonArray>();
  ASSERT_EQ(2U, arr.size());
  EXPECT_EQ(42U, arr[0]["heard_secs_ago"].as<uint32_t>());
  EXPECT_FALSE(arr[0]["heard_secs_ago"].isNull());
  EXPECT_TRUE(arr[1]["heard_secs_ago"].isNull());
  EXPECT_NE(nullptr, strstr(buffer, "\"heard_secs_ago\":null"));
  // The rest of the entry still renders.
  EXPECT_STREQ("8899AABBCCDDEEFF", arr[1]["pubkey"].as<const char*>());
  EXPECT_STREQ("responded", arr[1]["status"].as<const char*>());
}

TEST(MQTTPayloadBuilder, NeighborsMessageUnknownHeardAgeFitsMeasuredWidth) {
  // Paced discovery measures each entry with a known UINT32_MAX age; a null age
  // must never serialize wider than what that reserved.
  MQTTPayloadBuilder::NeighborsMessageEntry measured = {
      "0011223344556677", 9.75f, UINT32_MAX, "DEN", "responded"};
  MQTTPayloadBuilder::NeighborsMessageEntry unknown = measured;
  unknown.heard_unknown = true;
  unknown.heard_secs_ago = 0;

  EXPECT_LE(MQTTPayloadBuilder::measureNeighborsMessageEntry(unknown),
            MQTTPayloadBuilder::measureNeighborsMessageEntry(measured));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
