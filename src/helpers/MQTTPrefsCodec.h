#pragma once

#include <string.h>

#include "MQTTPacketFilter.h"
#include "MQTTPrefsStorage.h"

#ifdef WITH_MQTT_BRIDGE

// Pure /mqtt_prefs format classification and field-copy migration. Production
// reads directly into the selected layout; this header never requires a second
// large staging buffer.
namespace MQTTPrefsCodec {

enum class Source : uint8_t {
  Defaults,
  Current,
  LegacyPreSlot,
  LegacyThreeSlotBase,
  LegacyThreeSlot,
  LegacySixSlotBase,
  LegacySixSlotAudience,
  LegacySixSlotAudienceRx,
  LegacySixSlot,
  UnsupportedVersion,
  Corrupt,
};

struct DecodePlan {
  Source source;
  bool rewrite_legacy;
  bool preserve_file;
  // False means the decoded payload stops before snmp_enabled, so production
  // may apply a captured observer tail from legacy /com_prefs.
  bool observer_fields_present;
  size_t payload_len;
};

static const size_t kV1PreObserverPayloadSize = MQTT_PREFS_V1_PRE_OBSERVER_PAYLOAD_SIZE;
static const size_t kV1PreNeighborsPayloadSize = MQTT_PREFS_V1_PRE_NEIGHBORS_PAYLOAD_SIZE;
static const size_t kV1PreFilterPayloadSize = MQTT_PREFS_V1_PRE_FILTER_PAYLOAD_SIZE;
static const size_t kV1BaselinePayloadSize = MQTT_PREFS_V1_FULL_PAYLOAD_SIZE;
static const size_t kEncodedSize = sizeof(MQTTPrefsHeader) + kV1BaselinePayloadSize;

// Shortest payload length that still round-trips this configuration.
//
// The packet-filter tail is the only optional part of the current layout, and
// its default (all types) is exactly what a pre-filter decoder supplies for a
// missing tail. So a device whose filters are all default keeps writing the
// 2864-byte payload that pre-filter firmware can still read. That matters
// because /mqtt_prefs also carries the WiFi credentials: an unrecognised
// longer payload sends older firmware to defaults with no network, and it
// refuses to overwrite the file, so the node cannot be recovered over the air.
// Touching any filter opts that node into the longer payload — a deliberate,
// operator-initiated trade rather than a side effect of upgrading.
inline size_t payloadLenFor(const MQTTPrefs& prefs) {
  return MQTTPacketFilter::allMasksDefault(prefs.mqtt_slot_packet_filter,
                                           MQTT_PREFS_SLOT_COUNT)
      ? kV1PreFilterPayloadSize
      : kV1BaselinePayloadSize;
}

inline MQTTPrefsHeader makeHeader(size_t payload_len) {
  MQTTPrefsHeader header;
  memcpy(header.magic, MQTT_PREFS_MAGIC, sizeof(header.magic));
  header.version = MQTT_PREFS_VERSION;
  header.payload_len = static_cast<uint16_t>(payload_len);
  return header;
}

inline size_t encode(const MQTTPrefs& prefs, uint8_t* output, size_t output_size) {
  const size_t payload_len = payloadLenFor(prefs);
  const size_t encoded_size = sizeof(MQTTPrefsHeader) + payload_len;
  if (output == nullptr || output_size < encoded_size) return 0;
  const MQTTPrefsHeader header = makeHeader(payload_len);
  memcpy(output, &header, sizeof(header));
  memcpy(output + sizeof(header), &prefs, payload_len);
  return encoded_size;
}

inline bool isMagicPrefix(const uint8_t* input, size_t available) {
  if (input == nullptr || available == 0) return false;
  const size_t compare_len = available < sizeof(MQTT_PREFS_MAGIC)
      ? available : sizeof(MQTT_PREFS_MAGIC);
  return memcmp(input, MQTT_PREFS_MAGIC, compare_len) == 0;
}

inline DecodePlan corruptPlan() {
  return {Source::Corrupt, false, true, false, 0};
}

// Classify from the first eight bytes and the filesystem-reported file size.
// Headerless layouts are an explicit, audited whitelist. Call
// isPlausibleLegacy() after reading the selected layout and before rewriting:
// size alone cannot distinguish a valid legacy payload from arbitrary bytes.
inline DecodePlan classify(const uint8_t* prefix, size_t prefix_read, size_t file_size) {
  if (file_size == 0) return corruptPlan();
  if (prefix == nullptr || prefix_read == 0) return corruptPlan();
  const size_t expected_prefix = file_size < sizeof(MQTTPrefsHeader)
      ? file_size : sizeof(MQTTPrefsHeader);
  if (prefix_read < expected_prefix) return corruptPlan();

  if (file_size < sizeof(MQTTPrefsHeader) && isMagicPrefix(prefix, prefix_read)) {
    return corruptPlan();
  }
  if (file_size >= sizeof(MQTTPrefsHeader)) {
    MQTTPrefsHeader header;
    memcpy(&header, prefix, sizeof(header));
    if (memcmp(header.magic, MQTT_PREFS_MAGIC, sizeof(header.magic)) == 0) {
      if (header.version != MQTT_PREFS_VERSION) {
        return {Source::UnsupportedVersion, false, true, false, 0};
      }
      const size_t payload_available = file_size - sizeof(header);
      if (header.payload_len != payload_available) {
        return corruptPlan();
      }
      // A longer same-version payload was written by a later build that
      // appended fields. Within a version tag the layout is append-only, so
      // every byte this binary knows is present and correctly positioned — read
      // the baseline prefix and ignore the tail.
      //
      // This is the downgrade contract, and it is deliberately asymmetric:
      // refusing the file would cost the operator WiFi credentials and every
      // broker slot (a node with no network and no portal, recoverable only
      // over serial), whereas reading it costs only the settings the newer
      // build added. Losing a later feature's settings is the acceptable half.
      //
      // The tail survives until something actually writes: saveMQTTPrefs()
      // rewrites at this binary's own length, so a rollback that changes no
      // observer setting and is later rolled forward keeps the newer fields
      // intact. Only an explicit `set` while downgraded drops them.
      //
      // A layout change that is NOT a pure append must bump MQTT_PREFS_VERSION;
      // the version check above is what makes this rule safe.
      if (header.payload_len > kV1BaselinePayloadSize) {
        return {Source::Current, false, false, true, kV1BaselinePayloadSize};
      }
      if (header.payload_len == kV1BaselinePayloadSize) {
        return {Source::Current, false, false, true, kV1BaselinePayloadSize};
      }
      if (header.payload_len == kV1PreFilterPayloadSize) {
        // Written before the per-slot packet-filter tail. Defaults supply an
        // all-types mask for every slot.
        return {Source::Current, false, false, true, kV1PreFilterPayloadSize};
      }
      if (header.payload_len == kV1PreNeighborsPayloadSize) {
        // Written by observer/webconfig firmware before the neighbors tail
        // existed. The observer fields ARE present; only the neighbors tail is
        // missing, so it loads and keeps its defaults (off / 24h).
        return {Source::Current, false, false, true, kV1PreNeighborsPayloadSize};
      }
      if (header.payload_len == kV1PreObserverPayloadSize) {
        return {Source::Current, false, false, false, kV1PreObserverPayloadSize};
      }
      return corruptPlan();
    }
  }

  switch (file_size) {
    case sizeof(OldMQTTPrefs):
      return {Source::LegacyPreSlot, true, false, false, file_size};
    case sizeof(ThreeSlotBaseMQTTPrefs):
      return {Source::LegacyThreeSlotBase, true, false, false, file_size};
    case sizeof(ThreeSlotMQTTPrefs):
      return {Source::LegacyThreeSlot, true, false, false, file_size};
    case LEGACY6_BASE_SIZE:
      return {Source::LegacySixSlotBase, true, false, false, file_size};
    case LEGACY6_AUDIENCE_SIZE:
      return {Source::LegacySixSlotAudience, true, false, false, file_size};
    case LEGACY6_AUDIENCE_RX_SIZE:
      return {Source::LegacySixSlotAudienceRx, true, false, false, file_size};
    case sizeof(Legacy6SlotMQTTPrefs):
      return {Source::LegacySixSlot, true, false, false, file_size};
    default:
      return corruptPlan();
  }
}

inline bool looksLikePreWifiPower(const uint8_t* input, size_t size) {
  if (input == nullptr || size != sizeof(OldMQTTPrefs)) return false;
  // At byte 144 the newer layout has wifi_power_save (0..2); the older
  // layout has timezone_string[0]. A non-empty timezone is unambiguous.
  if (input[144] > 2) return true;
  // With an empty timezone, byte 177 is the older mqtt_server[0] but the
  // newer timezone_offset. If it cannot be an offset, it is also unambiguous.
  const int8_t newer_offset = static_cast<int8_t>(input[177]);
  // Byte 176 is the older timezone_offset but the final byte of the newer
  // timezone string (always NUL for values saved through the CLI).
  return input[144] == 0 &&
      (input[176] != 0 || newer_offset < -12 || newer_offset > 14);
}

// Headerless files have no checksum or magic, so their integrity cannot be
// proven. These checks intentionally reject obvious random data (unterminated
// strings and impossible flag/range values) without demanding application-level
// values that a real but sparsely configured device may not have set.
inline bool hasTerminatedText(const uint8_t* input, size_t size, size_t offset, size_t field_size) {
  if (input == nullptr || offset > size || field_size > size - offset) return false;
  for (size_t i = 0; i < field_size; ++i) {
    if (input[offset + i] == '\0') return true;
  }
  return false;
}

inline bool hasPlausibleCommonFields(const uint8_t* input, size_t size, bool pre_wifi_power) {
  if (input == nullptr || size < sizeof(OldMQTTPrefs)) return false;
  const size_t timezone_offset = pre_wifi_power
      ? offsetof(PreWifiPowerOldMQTTPrefs, timezone_string)
      : offsetof(OldMQTTPrefs, timezone_string);
  const size_t utc_offset = pre_wifi_power
      ? offsetof(PreWifiPowerOldMQTTPrefs, timezone_offset)
      : offsetof(OldMQTTPrefs, timezone_offset);
  const size_t timezone_size = 32;
  const int8_t timezone_hours = static_cast<int8_t>(input[utc_offset]);

  return input[offsetof(OldMQTTPrefs, mqtt_status_enabled)] <= 1 &&
      input[offsetof(OldMQTTPrefs, mqtt_packets_enabled)] <= 1 &&
      input[offsetof(OldMQTTPrefs, mqtt_raw_enabled)] <= 1 &&
      input[offsetof(OldMQTTPrefs, mqtt_tx_enabled)] <= 2 &&
      (pre_wifi_power || input[offsetof(OldMQTTPrefs, wifi_power_save)] <= 2) &&
      timezone_hours >= -12 && timezone_hours <= 14 &&
      hasTerminatedText(input, size, offsetof(OldMQTTPrefs, mqtt_origin),
                        32) &&
      hasTerminatedText(input, size, offsetof(OldMQTTPrefs, mqtt_iata),
                        8) &&
      hasTerminatedText(input, size, offsetof(OldMQTTPrefs, wifi_ssid),
                        32) &&
      hasTerminatedText(input, size, offsetof(OldMQTTPrefs, wifi_password),
                        64) &&
      hasTerminatedText(input, size, timezone_offset, timezone_size);
}

inline bool hasPlausibleSlotText(const uint8_t* input, size_t size, size_t slot_count,
                                 size_t preset_offset, size_t host_offset, size_t username_offset,
                                 size_t password_offset, size_t token_offset, size_t topic_offset,
                                 size_t audience_offset) {
  const size_t no_field = static_cast<size_t>(-1);
  for (size_t i = 0; i < slot_count; ++i) {
    if (!hasTerminatedText(input, size, preset_offset + i * 24, 24) ||
        !hasTerminatedText(input, size, host_offset + i * 64, 64) ||
        !hasTerminatedText(input, size, username_offset + i * 32, 32) ||
        !hasTerminatedText(input, size, password_offset + i * 64, 64) ||
        (token_offset != no_field && !hasTerminatedText(input, size, token_offset + i * 48, 48)) ||
        (topic_offset != no_field && !hasTerminatedText(input, size, topic_offset + i * 96, 96)) ||
        (audience_offset != no_field && !hasTerminatedText(input, size, audience_offset + i * 64, 64))) {
      return false;
    }
  }
  return true;
}

inline bool hasPlausibleSharedAuth(const uint8_t* input, size_t size, size_t owner_offset,
                                   size_t email_offset) {
  return hasTerminatedText(input, size, owner_offset, 65) &&
      hasTerminatedText(input, size, email_offset, 64);
}

inline bool isPlausibleLegacy(Source source, const uint8_t* input, size_t size) {
  const size_t no_field = static_cast<size_t>(-1);
  switch (source) {
    case Source::LegacyPreSlot: {
      if (size != sizeof(OldMQTTPrefs)) return false;
      const bool pre_wifi_power = looksLikePreWifiPower(input, size);
      const size_t server_offset = pre_wifi_power
          ? offsetof(PreWifiPowerOldMQTTPrefs, mqtt_server)
          : offsetof(OldMQTTPrefs, mqtt_server);
      const size_t username_offset = pre_wifi_power
          ? offsetof(PreWifiPowerOldMQTTPrefs, mqtt_username)
          : offsetof(OldMQTTPrefs, mqtt_username);
      const size_t password_offset = pre_wifi_power
          ? offsetof(PreWifiPowerOldMQTTPrefs, mqtt_password)
          : offsetof(OldMQTTPrefs, mqtt_password);
      const size_t us_enabled = pre_wifi_power
          ? offsetof(PreWifiPowerOldMQTTPrefs, mqtt_analyzer_us_enabled)
          : offsetof(OldMQTTPrefs, mqtt_analyzer_us_enabled);
      const size_t eu_enabled = pre_wifi_power
          ? offsetof(PreWifiPowerOldMQTTPrefs, mqtt_analyzer_eu_enabled)
          : offsetof(OldMQTTPrefs, mqtt_analyzer_eu_enabled);
      return hasPlausibleCommonFields(input, size, pre_wifi_power) &&
          input[us_enabled] <= 1 && input[eu_enabled] <= 1 &&
          hasTerminatedText(input, size, server_offset, 64) &&
          hasTerminatedText(input, size, username_offset, 32) &&
          hasTerminatedText(input, size, password_offset, 64) &&
          hasTerminatedText(input, size, pre_wifi_power
              ? offsetof(PreWifiPowerOldMQTTPrefs, mqtt_owner_public_key)
              : offsetof(OldMQTTPrefs, mqtt_owner_public_key), 65) &&
          hasTerminatedText(input, size, pre_wifi_power
              ? offsetof(PreWifiPowerOldMQTTPrefs, mqtt_email)
              : offsetof(OldMQTTPrefs, mqtt_email), 64);
    }
    case Source::LegacyThreeSlotBase:
      return size == sizeof(ThreeSlotBaseMQTTPrefs) &&
          hasPlausibleCommonFields(input, size, false) &&
          hasPlausibleSharedAuth(input, size,
              offsetof(ThreeSlotBaseMQTTPrefs, mqtt_owner_public_key),
              offsetof(ThreeSlotBaseMQTTPrefs, mqtt_email)) &&
          hasPlausibleSlotText(input, size, 3,
              offsetof(ThreeSlotBaseMQTTPrefs, mqtt_slot_preset),
              offsetof(ThreeSlotBaseMQTTPrefs, mqtt_slot_host),
              offsetof(ThreeSlotBaseMQTTPrefs, mqtt_slot_username),
              offsetof(ThreeSlotBaseMQTTPrefs, mqtt_slot_password),
              no_field, no_field, no_field);
    case Source::LegacyThreeSlot:
      return size == sizeof(ThreeSlotMQTTPrefs) &&
          hasPlausibleCommonFields(input, size, false) &&
          hasPlausibleSharedAuth(input, size,
              offsetof(ThreeSlotMQTTPrefs, mqtt_owner_public_key),
              offsetof(ThreeSlotMQTTPrefs, mqtt_email)) &&
          hasPlausibleSlotText(input, size, 3,
              offsetof(ThreeSlotMQTTPrefs, mqtt_slot_preset),
              offsetof(ThreeSlotMQTTPrefs, mqtt_slot_host),
              offsetof(ThreeSlotMQTTPrefs, mqtt_slot_username),
              offsetof(ThreeSlotMQTTPrefs, mqtt_slot_password),
              offsetof(ThreeSlotMQTTPrefs, mqtt_slot_token),
              offsetof(ThreeSlotMQTTPrefs, mqtt_slot_topic), no_field);
    case Source::LegacySixSlotBase:
    case Source::LegacySixSlotAudience:
    case Source::LegacySixSlotAudienceRx:
    case Source::LegacySixSlot: {
      const size_t expected_size = source == Source::LegacySixSlotBase ? LEGACY6_BASE_SIZE
          : source == Source::LegacySixSlotAudience ? LEGACY6_AUDIENCE_SIZE
          : source == Source::LegacySixSlotAudienceRx ? LEGACY6_AUDIENCE_RX_SIZE
          : sizeof(Legacy6SlotMQTTPrefs);
      const size_t audience_offset = source == Source::LegacySixSlotBase
          ? no_field : offsetof(Legacy6SlotMQTTPrefs, mqtt_slot_audience);
      const bool has_rx = source == Source::LegacySixSlotAudienceRx ||
          source == Source::LegacySixSlot;
      const bool has_ntp = source == Source::LegacySixSlot;
      return size == expected_size && hasPlausibleCommonFields(input, size, false) &&
          hasPlausibleSharedAuth(input, size,
              offsetof(Legacy6SlotMQTTPrefs, mqtt_owner_public_key),
              offsetof(Legacy6SlotMQTTPrefs, mqtt_email)) &&
          (!has_rx || input[offsetof(Legacy6SlotMQTTPrefs, mqtt_rx_enabled)] <= 1) &&
          (!has_ntp || hasTerminatedText(input, size,
              offsetof(Legacy6SlotMQTTPrefs, mqtt_ntp_server), 64)) &&
          hasPlausibleSlotText(input, size, MQTT_PREFS_SLOT_COUNT,
              offsetof(Legacy6SlotMQTTPrefs, mqtt_slot_preset),
              offsetof(Legacy6SlotMQTTPrefs, mqtt_slot_host),
              offsetof(Legacy6SlotMQTTPrefs, mqtt_slot_username),
              offsetof(Legacy6SlotMQTTPrefs, mqtt_slot_password),
              offsetof(Legacy6SlotMQTTPrefs, mqtt_slot_token),
              offsetof(Legacy6SlotMQTTPrefs, mqtt_slot_topic), audience_offset);
    }
    default:
      return false;
  }
}

inline void migratePreSlot(const OldMQTTPrefs& old_prefs, MQTTPrefs* prefs) {
  memcpy(prefs->mqtt_origin, old_prefs.mqtt_origin, sizeof(prefs->mqtt_origin));
  memcpy(prefs->mqtt_iata, old_prefs.mqtt_iata, sizeof(prefs->mqtt_iata));
  prefs->mqtt_status_enabled = old_prefs.mqtt_status_enabled;
  prefs->mqtt_packets_enabled = old_prefs.mqtt_packets_enabled;
  prefs->mqtt_raw_enabled = old_prefs.mqtt_raw_enabled;
  prefs->mqtt_tx_enabled = old_prefs.mqtt_tx_enabled;
  prefs->mqtt_status_interval = old_prefs.mqtt_status_interval;
  memcpy(prefs->wifi_ssid, old_prefs.wifi_ssid, sizeof(prefs->wifi_ssid));
  memcpy(prefs->wifi_password, old_prefs.wifi_password, sizeof(prefs->wifi_password));
  prefs->wifi_power_save = old_prefs.wifi_power_save;
  memcpy(prefs->timezone_string, old_prefs.timezone_string, sizeof(prefs->timezone_string));
  prefs->timezone_offset = old_prefs.timezone_offset;
  memcpy(prefs->mqtt_owner_public_key, old_prefs.mqtt_owner_public_key,
         sizeof(prefs->mqtt_owner_public_key));
  memcpy(prefs->mqtt_email, old_prefs.mqtt_email, sizeof(prefs->mqtt_email));
  strncpy(prefs->mqtt_slot_preset[0], old_prefs.mqtt_analyzer_us_enabled == 1
      ? "analyzer-us" : "none", sizeof(prefs->mqtt_slot_preset[0]) - 1);
  strncpy(prefs->mqtt_slot_preset[1], old_prefs.mqtt_analyzer_eu_enabled == 1
      ? "analyzer-eu" : "none", sizeof(prefs->mqtt_slot_preset[1]) - 1);
  if (old_prefs.mqtt_server[0] != '\0' && old_prefs.mqtt_port > 0) {
    strncpy(prefs->mqtt_slot_preset[2], "custom", sizeof(prefs->mqtt_slot_preset[2]) - 1);
    strncpy(prefs->mqtt_slot_host[2], old_prefs.mqtt_server,
            sizeof(prefs->mqtt_slot_host[2]) - 1);
    prefs->mqtt_slot_port[2] = old_prefs.mqtt_port;
    strncpy(prefs->mqtt_slot_username[2], old_prefs.mqtt_username,
            sizeof(prefs->mqtt_slot_username[2]) - 1);
    strncpy(prefs->mqtt_slot_password[2], old_prefs.mqtt_password,
            sizeof(prefs->mqtt_slot_password[2]) - 1);
  } else {
    strncpy(prefs->mqtt_slot_preset[2], "none", sizeof(prefs->mqtt_slot_preset[2]) - 1);
  }
}

inline void migratePreWifiPower(const PreWifiPowerOldMQTTPrefs& old_prefs, MQTTPrefs* prefs) {
  OldMQTTPrefs normalized = {};
  memcpy(normalized.mqtt_origin, old_prefs.mqtt_origin, sizeof(normalized.mqtt_origin));
  memcpy(normalized.mqtt_iata, old_prefs.mqtt_iata, sizeof(normalized.mqtt_iata));
  normalized.mqtt_status_enabled = old_prefs.mqtt_status_enabled;
  normalized.mqtt_packets_enabled = old_prefs.mqtt_packets_enabled;
  normalized.mqtt_raw_enabled = old_prefs.mqtt_raw_enabled;
  normalized.mqtt_tx_enabled = old_prefs.mqtt_tx_enabled;
  normalized.mqtt_status_interval = old_prefs.mqtt_status_interval;
  memcpy(normalized.wifi_ssid, old_prefs.wifi_ssid, sizeof(normalized.wifi_ssid));
  memcpy(normalized.wifi_password, old_prefs.wifi_password, sizeof(normalized.wifi_password));
  normalized.wifi_power_save = prefs->wifi_power_save;  // field did not exist yet
  memcpy(normalized.timezone_string, old_prefs.timezone_string, sizeof(normalized.timezone_string));
  normalized.timezone_offset = old_prefs.timezone_offset;
  memcpy(normalized.mqtt_server, old_prefs.mqtt_server, sizeof(normalized.mqtt_server));
  normalized.mqtt_port = old_prefs.mqtt_port;
  memcpy(normalized.mqtt_username, old_prefs.mqtt_username, sizeof(normalized.mqtt_username));
  memcpy(normalized.mqtt_password, old_prefs.mqtt_password, sizeof(normalized.mqtt_password));
  normalized.mqtt_analyzer_us_enabled = old_prefs.mqtt_analyzer_us_enabled;
  normalized.mqtt_analyzer_eu_enabled = old_prefs.mqtt_analyzer_eu_enabled;
  memcpy(normalized.mqtt_owner_public_key, old_prefs.mqtt_owner_public_key,
         sizeof(normalized.mqtt_owner_public_key));
  memcpy(normalized.mqtt_email, old_prefs.mqtt_email, sizeof(normalized.mqtt_email));
  migratePreSlot(normalized, prefs);
}

template <typename T>
inline void migrateThreeSlotCommon(const T& old_prefs, MQTTPrefs* prefs) {
  memcpy(prefs->mqtt_origin, old_prefs.mqtt_origin, sizeof(prefs->mqtt_origin));
  memcpy(prefs->mqtt_iata, old_prefs.mqtt_iata, sizeof(prefs->mqtt_iata));
  prefs->mqtt_status_enabled = old_prefs.mqtt_status_enabled;
  prefs->mqtt_packets_enabled = old_prefs.mqtt_packets_enabled;
  prefs->mqtt_raw_enabled = old_prefs.mqtt_raw_enabled;
  prefs->mqtt_tx_enabled = old_prefs.mqtt_tx_enabled;
  prefs->mqtt_status_interval = old_prefs.mqtt_status_interval;
  memcpy(prefs->wifi_ssid, old_prefs.wifi_ssid, sizeof(prefs->wifi_ssid));
  memcpy(prefs->wifi_password, old_prefs.wifi_password, sizeof(prefs->wifi_password));
  prefs->wifi_power_save = old_prefs.wifi_power_save;
  memcpy(prefs->timezone_string, old_prefs.timezone_string, sizeof(prefs->timezone_string));
  prefs->timezone_offset = old_prefs.timezone_offset;
  for (int i = 0; i < 3; i++) {
    memcpy(prefs->mqtt_slot_preset[i], old_prefs.mqtt_slot_preset[i], sizeof(prefs->mqtt_slot_preset[i]));
    memcpy(prefs->mqtt_slot_host[i], old_prefs.mqtt_slot_host[i], sizeof(prefs->mqtt_slot_host[i]));
    prefs->mqtt_slot_port[i] = old_prefs.mqtt_slot_port[i];
    memcpy(prefs->mqtt_slot_username[i], old_prefs.mqtt_slot_username[i], sizeof(prefs->mqtt_slot_username[i]));
    memcpy(prefs->mqtt_slot_password[i], old_prefs.mqtt_slot_password[i], sizeof(prefs->mqtt_slot_password[i]));
  }
  memcpy(prefs->mqtt_owner_public_key, old_prefs.mqtt_owner_public_key, sizeof(prefs->mqtt_owner_public_key));
  memcpy(prefs->mqtt_email, old_prefs.mqtt_email, sizeof(prefs->mqtt_email));
}

inline void migrateThreeSlot(const ThreeSlotBaseMQTTPrefs& old_prefs, MQTTPrefs* prefs) {
  migrateThreeSlotCommon(old_prefs, prefs);
}

inline void migrateThreeSlot(const ThreeSlotMQTTPrefs& old_prefs, MQTTPrefs* prefs) {
  migrateThreeSlotCommon(old_prefs, prefs);
  for (int i = 0; i < 3; i++) {
    memcpy(prefs->mqtt_slot_token[i], old_prefs.mqtt_slot_token[i], sizeof(prefs->mqtt_slot_token[i]));
    memcpy(prefs->mqtt_slot_topic[i], old_prefs.mqtt_slot_topic[i], sizeof(prefs->mqtt_slot_topic[i]));
  }
}

inline void migrateLegacySixSlotCommon(const Legacy6SlotMQTTPrefs& old_prefs, MQTTPrefs* prefs) {
  memcpy(prefs->mqtt_origin, old_prefs.mqtt_origin, sizeof(prefs->mqtt_origin));
  memcpy(prefs->mqtt_iata, old_prefs.mqtt_iata, sizeof(prefs->mqtt_iata));
  prefs->mqtt_status_enabled = old_prefs.mqtt_status_enabled;
  prefs->mqtt_packets_enabled = old_prefs.mqtt_packets_enabled;
  prefs->mqtt_raw_enabled = old_prefs.mqtt_raw_enabled;
  prefs->mqtt_tx_enabled = old_prefs.mqtt_tx_enabled;
  prefs->mqtt_status_interval = old_prefs.mqtt_status_interval;
  memcpy(prefs->wifi_ssid, old_prefs.wifi_ssid, sizeof(prefs->wifi_ssid));
  memcpy(prefs->wifi_password, old_prefs.wifi_password, sizeof(prefs->wifi_password));
  prefs->wifi_power_save = old_prefs.wifi_power_save;
  memcpy(prefs->timezone_string, old_prefs.timezone_string, sizeof(prefs->timezone_string));
  prefs->timezone_offset = old_prefs.timezone_offset;
  memcpy(prefs->mqtt_slot_preset, old_prefs.mqtt_slot_preset, sizeof(prefs->mqtt_slot_preset));
  memcpy(prefs->mqtt_slot_host, old_prefs.mqtt_slot_host, sizeof(prefs->mqtt_slot_host));
  memcpy(prefs->mqtt_slot_port, old_prefs.mqtt_slot_port, sizeof(prefs->mqtt_slot_port));
  memcpy(prefs->mqtt_slot_username, old_prefs.mqtt_slot_username, sizeof(prefs->mqtt_slot_username));
  memcpy(prefs->mqtt_slot_password, old_prefs.mqtt_slot_password, sizeof(prefs->mqtt_slot_password));
  memcpy(prefs->mqtt_owner_public_key, old_prefs.mqtt_owner_public_key, sizeof(prefs->mqtt_owner_public_key));
  memcpy(prefs->mqtt_email, old_prefs.mqtt_email, sizeof(prefs->mqtt_email));
  memcpy(prefs->mqtt_slot_token, old_prefs.mqtt_slot_token, sizeof(prefs->mqtt_slot_token));
  memcpy(prefs->mqtt_slot_topic, old_prefs.mqtt_slot_topic, sizeof(prefs->mqtt_slot_topic));
}

inline void migrateLegacySixSlot(const Legacy6SlotMQTTPrefs& old_prefs, Source source,
                                 MQTTPrefs* prefs) {
  migrateLegacySixSlotCommon(old_prefs, prefs);
  if (source == Source::LegacySixSlotAudience || source == Source::LegacySixSlotAudienceRx ||
      source == Source::LegacySixSlot) {
    memcpy(prefs->mqtt_slot_audience, old_prefs.mqtt_slot_audience,
           sizeof(prefs->mqtt_slot_audience));
  }
  if (source == Source::LegacySixSlotAudienceRx || source == Source::LegacySixSlot) {
    prefs->mqtt_rx_enabled = old_prefs.mqtt_rx_enabled;
  }
  if (source == Source::LegacySixSlot) {
    memcpy(prefs->mqtt_ntp_server, old_prefs.mqtt_ntp_server,
           sizeof(prefs->mqtt_ntp_server));
  }
}

}  // namespace MQTTPrefsCodec

#endif  // WITH_MQTT_BRIDGE
