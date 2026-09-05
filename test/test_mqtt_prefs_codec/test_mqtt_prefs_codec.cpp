#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#define WITH_MQTT_BRIDGE 1
#include "helpers/MQTTPrefsCodec.h"
#include "helpers/MQTTPacketFilter.h"

namespace Codec = MQTTPrefsCodec;

namespace {

MQTTPrefs defaults() {
  MQTTPrefs prefs = {};
  prefs.mqtt_status_enabled = 1;
  prefs.mqtt_packets_enabled = 1;
  prefs.mqtt_tx_enabled = 2;
  prefs.mqtt_rx_enabled = 1;
  prefs.mqtt_status_interval = 300000;
  prefs.wifi_power_save = 1;
  for (int i = 0; i < MQTT_PREFS_SLOT_COUNT; ++i) {
    strncpy(prefs.mqtt_slot_preset[i], "none", sizeof(prefs.mqtt_slot_preset[i]) - 1);
    prefs.mqtt_slot_packet_filter[i] = MQTTPacketFilter::kAllPacketTypes;
  }
  strncpy(prefs.snmp_community, "public", sizeof(prefs.snmp_community) - 1);
  prefs.radio_watchdog_minutes = 5;
  prefs.alert_wifi_minutes = 30;
  prefs.alert_mqtt_minutes = 240;
  prefs.alert_min_interval_min = 60;
  return prefs;
}

void writeText(std::vector<uint8_t>* bytes, size_t offset, const char* value) {
  const size_t length = strlen(value);
  ASSERT_LE(offset + length, bytes->size());
  memcpy(bytes->data() + offset, value, length);
}

void writeLe16(std::vector<uint8_t>* bytes, size_t offset, uint16_t value) {
  ASSERT_LE(offset + 2, bytes->size());
  (*bytes)[offset] = static_cast<uint8_t>(value & 0xff);
  (*bytes)[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void writeHeader(std::vector<uint8_t>* bytes, uint16_t version, uint16_t payload_len) {
  ASSERT_GE(bytes->size(), sizeof(MQTTPrefsHeader));
  (*bytes)[0] = MQTT_PREFS_MAGIC[0];
  (*bytes)[1] = MQTT_PREFS_MAGIC[1];
  (*bytes)[2] = MQTT_PREFS_MAGIC[2];
  (*bytes)[3] = MQTT_PREFS_MAGIC[3];
  writeLe16(bytes, 4, version);
  writeLe16(bytes, 6, payload_len);
}

Codec::DecodePlan classify(const std::vector<uint8_t>& bytes) {
  const size_t prefix_size = bytes.size() < sizeof(MQTTPrefsHeader)
      ? bytes.size() : sizeof(MQTTPrefsHeader);
  return Codec::classify(bytes.data(), prefix_size, bytes.size());
}

void fillHighEntropy(std::vector<uint8_t>* bytes) {
  uint32_t state = 0x89abcdef;
  for (size_t i = 0; i < bytes->size(); ++i) {
    state = state * 1664525u + 1013904223u;
    // Keep every byte non-NUL so this is a useful corruption fixture rather
    // than a sparse/default-like legacy file.
    (*bytes)[i] = static_cast<uint8_t>((state >> 24) | 0x80);
  }
}

}  // namespace

TEST(MQTTPrefsCodec, MigratesPostWifiPowerPreSlotFixture) {
  // Frozen post-WiFi-power pre-slot offsets: status=44, ssid=48, power=144,
  // timezone=145, server=178, port=242. Do not derive this fixture from structs.
  std::vector<uint8_t> bytes(472, 0);
  writeText(&bytes, 0, "legacy-node");
  writeText(&bytes, 32, "YYZ");
  bytes[40] = 1;
  bytes[41] = 1;
  bytes[43] = 2;
  bytes[144] = 2;
  writeText(&bytes, 145, "UTC");
  writeText(&bytes, 178, "broker.example");
  writeLe16(&bytes, 242, 1883);
  writeText(&bytes, 244, "alice");
  writeText(&bytes, 276, "secret");
  bytes[340] = 1;

  const Codec::DecodePlan plan = classify(bytes);
  ASSERT_EQ(Codec::Source::LegacyPreSlot, plan.source);
  ASSERT_TRUE(plan.rewrite_legacy);
  ASSERT_FALSE(Codec::looksLikePreWifiPower(bytes.data(), bytes.size()));
  ASSERT_TRUE(Codec::isPlausibleLegacy(plan.source, bytes.data(), bytes.size()));

  OldMQTTPrefs old_prefs = {};
  memcpy(&old_prefs, bytes.data(), sizeof(old_prefs));
  MQTTPrefs prefs = defaults();
  Codec::migratePreSlot(old_prefs, &prefs);

  EXPECT_STREQ("legacy-node", prefs.mqtt_origin);
  EXPECT_STREQ("analyzer-us", prefs.mqtt_slot_preset[0]);
  EXPECT_STREQ("custom", prefs.mqtt_slot_preset[2]);
  EXPECT_STREQ("broker.example", prefs.mqtt_slot_host[2]);
  EXPECT_EQ(1883, prefs.mqtt_slot_port[2]);
  EXPECT_STREQ("secret", prefs.mqtt_slot_password[2]);
}

TEST(MQTTPrefsCodec, MigratesPreWifiPowerFixtureWithConservativeHeuristic) {
  // In the pre-WiFi-power variant timezone starts at 144 and server at 177;
  // port still aligns at 242. A non-empty timezone makes the layout unambiguous.
  std::vector<uint8_t> bytes(472, 0);
  writeText(&bytes, 0, "pre-power");
  writeText(&bytes, 144, "PST8PDT");
  writeText(&bytes, 177, "old-broker");
  writeLe16(&bytes, 242, 8883);

  ASSERT_TRUE(Codec::looksLikePreWifiPower(bytes.data(), bytes.size()));
  ASSERT_TRUE(Codec::isPlausibleLegacy(Codec::Source::LegacyPreSlot,
                                       bytes.data(), bytes.size()));
  PreWifiPowerOldMQTTPrefs old_prefs = {};
  memcpy(&old_prefs, bytes.data(), sizeof(old_prefs));
  MQTTPrefs prefs = defaults();
  Codec::migratePreWifiPower(old_prefs, &prefs);

  EXPECT_STREQ("PST8PDT", prefs.timezone_string);
  EXPECT_STREQ("old-broker", prefs.mqtt_slot_host[2]);
  EXPECT_EQ(8883, prefs.mqtt_slot_port[2]);
  EXPECT_EQ(1, prefs.wifi_power_save);  // retained current default; old layout had none
}

TEST(MQTTPrefsCodec, DetectsPreWifiPowerFixtureWithOnlyUtcOffsetConfigured) {
  std::vector<uint8_t> bytes(472, 0);
  bytes[176] = static_cast<uint8_t>(-8);

  EXPECT_TRUE(Codec::looksLikePreWifiPower(bytes.data(), bytes.size()));
}

TEST(MQTTPrefsCodec, MigratesThreeSlotBaseAndExtendedFixtures) {
  // Frozen 3-slot offsets: presets=178, hosts=250, ports=442, users=448,
  // passwords=544, owner=736, tokens=1030, topics=1174. The base file has
  // two tail-padding bytes, so its frozen size is 1032.
  std::vector<uint8_t> base(1032, 0);
  writeText(&base, 0, "three-base");
  writeText(&base, 178, "meshmapper");
  writeText(&base, 250, "three.example");
  writeLe16(&base, 442, 1884);
  const Codec::DecodePlan base_plan = classify(base);
  ASSERT_EQ(Codec::Source::LegacyThreeSlotBase, base_plan.source);
  ASSERT_TRUE(Codec::isPlausibleLegacy(base_plan.source, base.data(), base.size()));
  ThreeSlotBaseMQTTPrefs old_base = {};
  memcpy(&old_base, base.data(), sizeof(old_base));
  MQTTPrefs base_prefs = defaults();
  Codec::migrateThreeSlot(old_base, &base_prefs);
  EXPECT_STREQ("three-base", base_prefs.mqtt_origin);
  EXPECT_STREQ("meshmapper", base_prefs.mqtt_slot_preset[0]);
  EXPECT_EQ(1884, base_prefs.mqtt_slot_port[0]);
  EXPECT_STREQ("none", base_prefs.mqtt_slot_preset[3]);

  std::vector<uint8_t> extended(1464, 0);
  writeText(&extended, 0, "three-extended");
  writeText(&extended, 178 + 24, "custom");
  writeText(&extended, 250 + 64, "slot-two.example");
  writeText(&extended, 1030 + 48, "token-two");
  writeText(&extended, 1174 + 96, "custom/{type}");
  const Codec::DecodePlan extended_plan = classify(extended);
  ASSERT_EQ(Codec::Source::LegacyThreeSlot, extended_plan.source);
  ASSERT_TRUE(Codec::isPlausibleLegacy(extended_plan.source, extended.data(), extended.size()));
  ThreeSlotMQTTPrefs old_extended = {};
  memcpy(&old_extended, extended.data(), sizeof(old_extended));
  MQTTPrefs extended_prefs = defaults();
  Codec::migrateThreeSlot(old_extended, &extended_prefs);
  EXPECT_STREQ("custom", extended_prefs.mqtt_slot_preset[1]);
  EXPECT_STREQ("token-two", extended_prefs.mqtt_slot_token[1]);
  EXPECT_STREQ("custom/{type}", extended_prefs.mqtt_slot_topic[1]);
}

TEST(MQTTPrefsCodec, MigratesAllLegacySixSlotPrefixesWithoutClobberingDefaults) {
  // Frozen 6-slot offsets: presets=178, hosts=322, ports=706, tokens=1588,
  // topics=1876, audience=2452, rx=2836, ntp=2837.
  for (const size_t size : {size_t(2452), size_t(2836), size_t(2840), size_t(2904)}) {
    std::vector<uint8_t> bytes(size, 0);
    writeText(&bytes, 0, "six-slot");
    writeText(&bytes, 178 + 5 * 24, "custom");
    writeText(&bytes, 322 + 5 * 64, "six.example");
    writeText(&bytes, 1588 + 5 * 48, "token-six");
    writeText(&bytes, 1876 + 5 * 96, "six/{type}");
    if (size >= 2836) writeText(&bytes, 2452 + 5 * 64, "audience-six");
    if (size >= 2840) bytes[2836] = 0;
    if (size >= 2904) writeText(&bytes, 2837, "time.example");

    const Codec::DecodePlan plan = classify(bytes);
    const Codec::Source expected_source = size == 2452 ? Codec::Source::LegacySixSlotBase
        : size == 2836 ? Codec::Source::LegacySixSlotAudience
        : size == 2840 ? Codec::Source::LegacySixSlotAudienceRx
        : Codec::Source::LegacySixSlot;
    EXPECT_EQ(expected_source, plan.source) << size;
    ASSERT_TRUE(plan.rewrite_legacy);
    ASSERT_TRUE(Codec::isPlausibleLegacy(plan.source, bytes.data(), bytes.size())) << size;
    Legacy6SlotMQTTPrefs old_prefs = {};
    memcpy(&old_prefs, bytes.data(), bytes.size());
    MQTTPrefs prefs = defaults();
    Codec::migrateLegacySixSlot(old_prefs, plan.source, &prefs);

    EXPECT_STREQ("custom", prefs.mqtt_slot_preset[5]);
    EXPECT_STREQ("token-six", prefs.mqtt_slot_token[5]);
    if (size < 2836) {
      EXPECT_EQ('\0', prefs.mqtt_slot_audience[5][0]);
    } else {
      EXPECT_STREQ("audience-six", prefs.mqtt_slot_audience[5]);
    }
    if (size < 2840) {
      EXPECT_EQ(1, prefs.mqtt_rx_enabled) << size;
    } else {
      EXPECT_EQ(0, prefs.mqtt_rx_enabled);
    }
    if (size < 2904) {
      EXPECT_EQ('\0', prefs.mqtt_ntp_server[0]);
    } else {
      EXPECT_STREQ("time.example", prefs.mqtt_ntp_server);
    }
    for (int slot = 0; slot < MQTT_PREFS_SLOT_COUNT; ++slot) {
      EXPECT_EQ(MQTTPacketFilter::kAllPacketTypes,
                prefs.mqtt_slot_packet_filter[slot]) << size << ":" << slot;
    }
  }
}

TEST(MQTTPrefsCodec, CurrentVersionedPayloadRoundTripsExactly) {
  MQTTPrefs source = defaults();
  strncpy(source.mqtt_origin, "current-node", sizeof(source.mqtt_origin) - 1);
  strncpy(source.mqtt_slot_password[2], "preserve-me", sizeof(source.mqtt_slot_password[2]) - 1);
  strncpy(source.alert_region, "PNW", sizeof(source.alert_region) - 1);
  source.mqtt_neighbors_enabled = 1;
  source.mqtt_neighbors_interval = MQTT_NEIGHBORS_MAX_INTERVAL_MS;
  for (int i = 0; i < MQTT_PREFS_SLOT_COUNT; ++i) {
    source.mqtt_slot_packet_filter[i] = static_cast<uint16_t>(1u << i);
  }
  std::vector<uint8_t> bytes(Codec::kEncodedSize);
  ASSERT_EQ(Codec::kEncodedSize, Codec::encode(source, bytes.data(), bytes.size()));

  const Codec::DecodePlan plan = classify(bytes);
  ASSERT_EQ(Codec::Source::Current, plan.source);
  ASSERT_FALSE(plan.preserve_file);
  ASSERT_TRUE(plan.observer_fields_present);
  MQTTPrefs loaded = defaults();
  memcpy(&loaded, bytes.data() + sizeof(MQTTPrefsHeader), plan.payload_len);
  EXPECT_EQ(0, memcmp(&source, &loaded, sizeof(source)));
}

// /mqtt_prefs also carries the WiFi credentials, so a payload older firmware
// rejects strands a downgraded node with no network and no way to save. The
// filter tail is only written once it actually holds something.
TEST(MQTTPrefsCodec, DefaultFiltersKeepTheDowngradeReadablePayloadLength) {
  MQTTPrefs source = defaults();
  strncpy(source.wifi_ssid, "home-net", sizeof(source.wifi_ssid) - 1);
  strncpy(source.mqtt_slot_preset[0], "analyzer-us", sizeof(source.mqtt_slot_preset[0]) - 1);

  ASSERT_EQ(Codec::kV1PreFilterPayloadSize, Codec::payloadLenFor(source));
  std::vector<uint8_t> bytes(Codec::kEncodedSize, 0xEE);
  const size_t written = Codec::encode(source, bytes.data(), bytes.size());
  ASSERT_EQ(sizeof(MQTTPrefsHeader) + Codec::kV1PreFilterPayloadSize, written);
  EXPECT_LT(written, Codec::kEncodedSize);

  MQTTPrefsHeader header;
  memcpy(&header, bytes.data(), sizeof(header));
  EXPECT_EQ(Codec::kV1PreFilterPayloadSize, header.payload_len);
  // Nothing past the boundary was touched, so the file really is the short one.
  for (size_t i = written; i < bytes.size(); ++i) {
    EXPECT_EQ(0xEE, bytes[i]) << i;
  }

  bytes.resize(written);
  const Codec::DecodePlan plan = classify(bytes);
  ASSERT_EQ(Codec::Source::Current, plan.source);
  ASSERT_EQ(Codec::kV1PreFilterPayloadSize, plan.payload_len);
  ASSERT_FALSE(plan.preserve_file);

  MQTTPrefs loaded = defaults();
  memcpy(&loaded, bytes.data() + sizeof(MQTTPrefsHeader), plan.payload_len);
  EXPECT_EQ(0, memcmp(&source, &loaded, sizeof(source)));
}

TEST(MQTTPrefsCodec, AnyNonDefaultFilterOptsIntoTheLongerPayload) {
  MQTTPrefs source = defaults();
  strncpy(source.wifi_ssid, "home-net", sizeof(source.wifi_ssid) - 1);

  // "none" is as much a real setting as a subset, and must survive a reload.
  for (int slot = 0; slot < MQTT_PREFS_SLOT_COUNT; ++slot) {
    for (uint16_t mask : {static_cast<uint16_t>(0),
                          static_cast<uint16_t>(1u << 4),
                          static_cast<uint16_t>(MQTTPacketFilter::kAllPacketTypes & ~1u)}) {
      source.mqtt_slot_packet_filter[slot] = mask;
      ASSERT_EQ(Codec::kV1BaselinePayloadSize, Codec::payloadLenFor(source)) << slot;

      std::vector<uint8_t> bytes(Codec::kEncodedSize);
      ASSERT_EQ(Codec::kEncodedSize, Codec::encode(source, bytes.data(), bytes.size()));
      const Codec::DecodePlan plan = classify(bytes);
      ASSERT_EQ(Codec::kV1BaselinePayloadSize, plan.payload_len);

      MQTTPrefs loaded = defaults();
      memcpy(&loaded, bytes.data() + sizeof(MQTTPrefsHeader), plan.payload_len);
      EXPECT_EQ(mask, loaded.mqtt_slot_packet_filter[slot]) << slot;
      EXPECT_EQ(0, memcmp(&source, &loaded, sizeof(source)));
    }
    source.mqtt_slot_packet_filter[slot] = MQTTPacketFilter::kAllPacketTypes;
  }
  // Clearing the last non-default filter returns the node to the short payload.
  EXPECT_EQ(Codec::kV1PreFilterPayloadSize, Codec::payloadLenFor(source));
}

TEST(MQTTPrefsCodec, EncodeRefusesAnOutputBufferShorterThanItsChosenPayload) {
  MQTTPrefs shortest = defaults();
  const size_t short_size = sizeof(MQTTPrefsHeader) + Codec::kV1PreFilterPayloadSize;
  std::vector<uint8_t> bytes(Codec::kEncodedSize);

  EXPECT_EQ(0u, Codec::encode(shortest, bytes.data(), short_size - 1));
  EXPECT_EQ(short_size, Codec::encode(shortest, bytes.data(), short_size));
  EXPECT_EQ(0u, Codec::encode(shortest, nullptr, bytes.size()));

  // A buffer that fits the short payload is not enough for the long one.
  MQTTPrefs longest = defaults();
  longest.mqtt_slot_packet_filter[0] = 0;
  EXPECT_EQ(0u, Codec::encode(longest, bytes.data(), short_size));
  EXPECT_EQ(Codec::kEncodedSize, Codec::encode(longest, bytes.data(), bytes.size()));
}

TEST(MQTTPrefsCodec, PreFilterV1PayloadDefaultsEverySlotToAllTypes) {
  MQTTPrefs source = defaults();
  strncpy(source.mqtt_origin, "pre-filter-node", sizeof(source.mqtt_origin) - 1);
  source.mqtt_neighbors_enabled = 1;
  source.mqtt_neighbors_interval = MQTT_NEIGHBORS_MAX_INTERVAL_MS;
  for (int i = 0; i < MQTT_PREFS_SLOT_COUNT; ++i) {
    source.mqtt_slot_packet_filter[i] = 0;
  }

  std::vector<uint8_t> bytes(sizeof(MQTTPrefsHeader) + Codec::kV1PreFilterPayloadSize, 0);
  writeHeader(&bytes, MQTT_PREFS_VERSION,
              static_cast<uint16_t>(Codec::kV1PreFilterPayloadSize));
  memcpy(bytes.data() + sizeof(MQTTPrefsHeader), &source, Codec::kV1PreFilterPayloadSize);

  const Codec::DecodePlan plan = classify(bytes);
  ASSERT_EQ(Codec::Source::Current, plan.source);
  ASSERT_EQ(Codec::kV1PreFilterPayloadSize, plan.payload_len);
  ASSERT_TRUE(plan.observer_fields_present);
  ASSERT_FALSE(plan.preserve_file);

  MQTTPrefs loaded = defaults();
  memcpy(&loaded, bytes.data() + sizeof(MQTTPrefsHeader), plan.payload_len);
  EXPECT_STREQ("pre-filter-node", loaded.mqtt_origin);
  EXPECT_EQ(1u, loaded.mqtt_neighbors_enabled);
  EXPECT_EQ(MQTT_NEIGHBORS_MAX_INTERVAL_MS, loaded.mqtt_neighbors_interval);
  for (int i = 0; i < MQTT_PREFS_SLOT_COUNT; ++i) {
    EXPECT_EQ(MQTTPacketFilter::kAllPacketTypes,
              loaded.mqtt_slot_packet_filter[i]) << i;
  }
}

TEST(MQTTPrefsCodec, CompatibleShortV1PayloadPreservesDefaultsBeyondObserverBoundary) {
  MQTTPrefs source = defaults();
  strncpy(source.mqtt_origin, "short-v1-node", sizeof(source.mqtt_origin) - 1);
  strncpy(source.mqtt_ntp_server, "ntp.short.example", sizeof(source.mqtt_ntp_server) - 1);
  source.snmp_enabled = 1;
  strncpy(source.snmp_community, "do-not-copy", sizeof(source.snmp_community) - 1);
  source.radio_watchdog_minutes = 99;

  std::vector<uint8_t> bytes(sizeof(MQTTPrefsHeader) + Codec::kV1PreObserverPayloadSize, 0);
  writeHeader(&bytes, MQTT_PREFS_VERSION,
              static_cast<uint16_t>(Codec::kV1PreObserverPayloadSize));
  memcpy(bytes.data() + sizeof(MQTTPrefsHeader), &source, Codec::kV1PreObserverPayloadSize);

  const Codec::DecodePlan plan = classify(bytes);
  ASSERT_EQ(Codec::Source::Current, plan.source);
  ASSERT_EQ(Codec::kV1PreObserverPayloadSize, plan.payload_len);
  ASSERT_FALSE(plan.preserve_file);
  ASSERT_FALSE(plan.observer_fields_present);

  MQTTPrefs loaded = defaults();
  memcpy(&loaded, bytes.data() + sizeof(MQTTPrefsHeader), plan.payload_len);
  EXPECT_STREQ("short-v1-node", loaded.mqtt_origin);
  EXPECT_STREQ("ntp.short.example", loaded.mqtt_ntp_server);
  EXPECT_EQ(0, loaded.snmp_enabled);
  EXPECT_STREQ("public", loaded.snmp_community);
  EXPECT_EQ(5, loaded.radio_watchdog_minutes);
}

TEST(MQTTPrefsCodec, PreNeighborsV1PayloadLoadsObserverFieldsAndDefaultsNeighborsTail) {
  // A /mqtt_prefs written by observer/webconfig firmware before the neighbors
  // tail existed: full observer fields, 2860-byte v1 payload. It must still load
  // as Current (observer fields present) with the neighbors tail defaulted.
  MQTTPrefs source = defaults();
  strncpy(source.mqtt_origin, "pre-neighbors-node", sizeof(source.mqtt_origin) - 1);
  strncpy(source.alert_region, "PNW", sizeof(source.alert_region) - 1);
  source.snmp_enabled = 1;
  source.alert_enabled = 1;
  source.mqtt_neighbors_enabled = 0;            // old struct's byte 2857 was zero padding
  source.mqtt_neighbors_interval = 0x11223344;  // must NOT survive a 2860-byte read

  std::vector<uint8_t> bytes(sizeof(MQTTPrefsHeader) + Codec::kV1PreNeighborsPayloadSize, 0);
  writeHeader(&bytes, MQTT_PREFS_VERSION,
              static_cast<uint16_t>(Codec::kV1PreNeighborsPayloadSize));
  memcpy(bytes.data() + sizeof(MQTTPrefsHeader), &source, Codec::kV1PreNeighborsPayloadSize);

  const Codec::DecodePlan plan = classify(bytes);
  ASSERT_EQ(Codec::Source::Current, plan.source);
  ASSERT_EQ(Codec::kV1PreNeighborsPayloadSize, plan.payload_len);
  ASSERT_FALSE(plan.preserve_file);
  ASSERT_TRUE(plan.observer_fields_present);

  MQTTPrefs loaded = defaults();
  loaded.mqtt_neighbors_enabled = 1;                                   // pretend stale
  loaded.mqtt_neighbors_interval = MQTT_NEIGHBORS_DEFAULT_INTERVAL_MS;  // caller's defaulted tail
  memcpy(&loaded, bytes.data() + sizeof(MQTTPrefsHeader), plan.payload_len);

  EXPECT_STREQ("pre-neighbors-node", loaded.mqtt_origin);
  EXPECT_STREQ("PNW", loaded.alert_region);
  EXPECT_EQ(1, loaded.snmp_enabled);
  EXPECT_EQ(1, loaded.alert_enabled);
  // Enable flag sits at offset 2857 (inside the 2860 read) -> takes the file's 0.
  // Interval begins at 2860 (beyond the read) -> keeps the caller's default.
  EXPECT_EQ(0u, loaded.mqtt_neighbors_enabled);
  EXPECT_EQ(MQTT_NEIGHBORS_DEFAULT_INTERVAL_MS, loaded.mqtt_neighbors_interval);
  for (int i = 0; i < MQTT_PREFS_SLOT_COUNT; ++i) {
    EXPECT_EQ(MQTTPacketFilter::kAllPacketTypes,
              loaded.mqtt_slot_packet_filter[i]) << i;
  }
}

TEST(MQTTPrefsCodec, CorruptOrShortVersionedInputsArePreserved) {
  Codec::DecodePlan plan = Codec::classify(nullptr, 0, 0);
  EXPECT_EQ(Codec::Source::Corrupt, plan.source);
  EXPECT_TRUE(plan.preserve_file);

  std::vector<uint8_t> partial_magic = {MQTT_PREFS_MAGIC[0], MQTT_PREFS_MAGIC[1], MQTT_PREFS_MAGIC[2]};
  plan = classify(partial_magic);
  EXPECT_EQ(Codec::Source::Corrupt, plan.source);
  EXPECT_TRUE(plan.preserve_file);

  std::vector<uint8_t> short_payload(sizeof(MQTTPrefsHeader) + 4, 0);
  writeHeader(&short_payload, MQTT_PREFS_VERSION,
              static_cast<uint16_t>(Codec::kV1BaselinePayloadSize - 1));
  plan = classify(short_payload);
  EXPECT_EQ(Codec::Source::Corrupt, plan.source);
  EXPECT_TRUE(plan.preserve_file);

  std::vector<uint8_t> declared_short_with_full_body(
      sizeof(MQTTPrefsHeader) + Codec::kV1BaselinePayloadSize, 0);
  writeHeader(&declared_short_with_full_body, MQTT_PREFS_VERSION,
              static_cast<uint16_t>(Codec::kV1PreObserverPayloadSize));
  plan = classify(declared_short_with_full_body);
  EXPECT_EQ(Codec::Source::Corrupt, plan.source);
  EXPECT_TRUE(plan.preserve_file);

  std::vector<uint8_t> declared_full_with_short_body(
      sizeof(MQTTPrefsHeader) + Codec::kV1PreObserverPayloadSize, 0);
  writeHeader(&declared_full_with_short_body, MQTT_PREFS_VERSION,
              static_cast<uint16_t>(Codec::kV1BaselinePayloadSize));
  plan = classify(declared_full_with_short_body);
  EXPECT_EQ(Codec::Source::Corrupt, plan.source);
  EXPECT_TRUE(plan.preserve_file);

  std::vector<uint8_t> truncated(sizeof(MQTTPrefsHeader) + Codec::kV1BaselinePayloadSize - 1, 0);
  writeHeader(&truncated, MQTT_PREFS_VERSION,
              static_cast<uint16_t>(Codec::kV1BaselinePayloadSize));
  plan = classify(truncated);
  EXPECT_EQ(Codec::Source::Corrupt, plan.source);
  EXPECT_TRUE(plan.preserve_file);

  std::vector<uint8_t> trailing(sizeof(MQTTPrefsHeader) + Codec::kV1BaselinePayloadSize + 1, 0);
  writeHeader(&trailing, MQTT_PREFS_VERSION,
              static_cast<uint16_t>(Codec::kV1BaselinePayloadSize));
  plan = classify(trailing);
  EXPECT_EQ(Codec::Source::Corrupt, plan.source);
  EXPECT_TRUE(plan.preserve_file);
}

TEST(MQTTPrefsCodec, LegacyPlausibilityRejectsHighEntropyBytesAtEveryWhitelistedSize) {
  // A headerless raw struct has no checksum, so this only reduces false
  // migrations; it cannot prove that a plausible-looking file is authentic.
  for (const size_t size : {size_t(472), size_t(1032), size_t(1464), size_t(2452),
                            size_t(2836), size_t(2840), size_t(2904)}) {
    std::vector<uint8_t> bytes(size, 0);
    fillHighEntropy(&bytes);
    const Codec::DecodePlan plan = classify(bytes);
    ASSERT_TRUE(plan.rewrite_legacy) << size;
    EXPECT_FALSE(Codec::isPlausibleLegacy(plan.source, bytes.data(), bytes.size())) << size;
  }
}

TEST(MQTTPrefsCodec, UnsupportedHeaderlessSizesArePreserved) {
  // 3024 was produced briefly before versioning, but repository history says
  // that raw observer-tail form was not shipped. Preserve it rather than guess.
  for (const size_t size : {size_t(471), size_t(473), size_t(1465), size_t(2905), size_t(3024)}) {
    std::vector<uint8_t> bytes(size, 0);
    const Codec::DecodePlan plan = classify(bytes);
    EXPECT_EQ(Codec::Source::Corrupt, plan.source) << size;
    EXPECT_TRUE(plan.preserve_file) << size;
  }
}

// A different version tag means the layout may have changed shape, so it is
// still refused outright. That refusal is what makes reading longer same-version
// payloads safe, so the two belong in one test.
TEST(MQTTPrefsCodec, ADifferentVersionTagIsStillRefusedAndHeld) {
  std::vector<uint8_t> newer(sizeof(MQTTPrefsHeader), 0);
  writeHeader(&newer, MQTT_PREFS_VERSION + 1, 0);
  const Codec::DecodePlan plan = classify(newer);
  EXPECT_EQ(Codec::Source::UnsupportedVersion, plan.source);
  EXPECT_TRUE(plan.preserve_file);
}

// The downgrade contract: a v1 payload longer than this build's baseline was
// appended to by a later build, so the baseline prefix is present verbatim.
// Read it and drop the tail — never refuse the file, which would cost the
// operator WiFi and every broker slot to save settings they don't understand.
TEST(MQTTPrefsCodec, LongerSameVersionPayloadLoadsTheBaselineAndIgnoresTheTail) {
  MQTTPrefs source = defaults();
  strncpy(source.mqtt_origin, "future-node", sizeof(source.mqtt_origin) - 1);
  strncpy(source.wifi_ssid, "field-ssid", sizeof(source.wifi_ssid) - 1);
  strncpy(source.wifi_password, "field-secret", sizeof(source.wifi_password) - 1);
  strncpy(source.mqtt_iata, "SEA", sizeof(source.mqtt_iata) - 1);
  strncpy(source.mqtt_slot_preset[0], "meshrank", sizeof(source.mqtt_slot_preset[0]) - 1);
  source.mqtt_neighbors_enabled = 1;
  for (int i = 0; i < MQTT_PREFS_SLOT_COUNT; ++i) {
    source.mqtt_slot_packet_filter[i] = static_cast<uint16_t>(1u << i);
  }

  // Baseline image plus a 40-byte tail of fields this build has never heard of.
  const size_t kTail = 40;
  const size_t payload_len = Codec::kV1BaselinePayloadSize + kTail;
  std::vector<uint8_t> bytes(sizeof(MQTTPrefsHeader) + payload_len, 0xA5);
  writeHeader(&bytes, MQTT_PREFS_VERSION, static_cast<uint16_t>(payload_len));
  memcpy(bytes.data() + sizeof(MQTTPrefsHeader), &source, sizeof(source));

  const Codec::DecodePlan plan = classify(bytes);
  ASSERT_EQ(Codec::Source::Current, plan.source);
  EXPECT_FALSE(plan.preserve_file) << "refusing the file would strand the node";
  EXPECT_TRUE(plan.observer_fields_present);
  EXPECT_FALSE(plan.rewrite_legacy);
  ASSERT_EQ(Codec::kV1BaselinePayloadSize, plan.payload_len);

  // Reading plan.payload_len bytes recovers this build's whole struct exactly,
  // and cannot run past it into the unknown tail.
  MQTTPrefs loaded = defaults();
  memcpy(&loaded, bytes.data() + sizeof(MQTTPrefsHeader), plan.payload_len);
  EXPECT_EQ(0, memcmp(&source, &loaded, sizeof(source)));
  EXPECT_STREQ("future-node", loaded.mqtt_origin);
  EXPECT_STREQ("field-ssid", loaded.wifi_ssid);
  EXPECT_STREQ("field-secret", loaded.wifi_password);
  EXPECT_STREQ("meshrank", loaded.mqtt_slot_preset[0]);
  EXPECT_EQ(1u, loaded.mqtt_neighbors_enabled);
  EXPECT_EQ(1u, loaded.mqtt_slot_packet_filter[0]);
}

// The rule has to hold for any appended size, including a single byte and a
// tail far larger than the baseline.
TEST(MQTTPrefsCodec, EveryLongerSameVersionLengthReadsTheBaseline) {
  for (const size_t tail : {size_t(1), size_t(2), size_t(12), size_t(64),
                            size_t(512), size_t(4096)}) {
    const size_t payload_len = Codec::kV1BaselinePayloadSize + tail;
    std::vector<uint8_t> bytes(sizeof(MQTTPrefsHeader) + payload_len, 0);
    writeHeader(&bytes, MQTT_PREFS_VERSION, static_cast<uint16_t>(payload_len));
    const Codec::DecodePlan plan = classify(bytes);
    EXPECT_EQ(Codec::Source::Current, plan.source) << tail;
    EXPECT_FALSE(plan.preserve_file) << tail;
    EXPECT_EQ(Codec::kV1BaselinePayloadSize, plan.payload_len) << tail;
  }
}

// A length that merely *declares* a longer payload without the bytes to back it
// is still corrupt — the header/file-size agreement check must run first.
TEST(MQTTPrefsCodec, LongerDeclaredLengthWithoutTheBytesIsStillCorrupt) {
  std::vector<uint8_t> bytes(sizeof(MQTTPrefsHeader) + Codec::kV1BaselinePayloadSize, 0);
  writeHeader(&bytes, MQTT_PREFS_VERSION,
              static_cast<uint16_t>(Codec::kV1BaselinePayloadSize + 16));
  const Codec::DecodePlan plan = classify(bytes);
  EXPECT_EQ(Codec::Source::Corrupt, plan.source);
  EXPECT_TRUE(plan.preserve_file);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
