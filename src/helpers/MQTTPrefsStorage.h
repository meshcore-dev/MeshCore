#pragma once

#include <stddef.h>
#include <stdint.h>

// /mqtt_prefs is a raw binary persistence format. Keep the layout-only types
// independent from CommonCLI so the migration decoder can be tested on the host
// without pulling in Arduino, filesystem, or radio dependencies.
#ifdef WITH_MQTT_BRIDGE

// Must match MAX_MQTT_SLOTS in MQTTPresets.h. CommonCLI.h enforces that link on
// firmware builds; keeping this header standalone avoids importing preset data
// into host migration tests.
static const int MQTT_PREFS_SLOT_COUNT = 6;

// Old MQTT preferences layout (pre-slot firmware) — used only for migration detection.
struct OldMQTTPrefs {
  char mqtt_origin[32];
  char mqtt_iata[8];
  uint8_t mqtt_status_enabled;
  uint8_t mqtt_packets_enabled;
  uint8_t mqtt_raw_enabled;
  uint8_t mqtt_tx_enabled;
  uint32_t mqtt_status_interval;
  char wifi_ssid[32];
  char wifi_password[64];
  uint8_t wifi_power_save;
  char timezone_string[32];
  int8_t timezone_offset;
  char mqtt_server[64];
  uint16_t mqtt_port;
  char mqtt_username[32];
  char mqtt_password[64];
  uint8_t mqtt_analyzer_us_enabled;
  uint8_t mqtt_analyzer_eu_enabled;
  char mqtt_owner_public_key[65];
  char mqtt_email[64];
};

// The pre-WiFi-power pre-slot layout has the same frozen size as
// OldMQTTPrefs, but timezone/server start one byte earlier. A conservative
// classifier distinguishes meaningful configurations before migration.
struct PreWifiPowerOldMQTTPrefs {
  char mqtt_origin[32];
  char mqtt_iata[8];
  uint8_t mqtt_status_enabled;
  uint8_t mqtt_packets_enabled;
  uint8_t mqtt_raw_enabled;
  uint8_t mqtt_tx_enabled;
  uint32_t mqtt_status_interval;
  char wifi_ssid[32];
  char wifi_password[64];
  char timezone_string[32];
  int8_t timezone_offset;
  char mqtt_server[64];
  uint16_t mqtt_port;
  char mqtt_username[32];
  char mqtt_password[64];
  uint8_t mqtt_analyzer_us_enabled;
  uint8_t mqtt_analyzer_eu_enabled;
  char mqtt_owner_public_key[65];
  char mqtt_email[64];
};

// MQTT preferences stored separately from NodePrefs to avoid upstream layout
// conflicts. The full layout is the frozen v1 payload baseline. The prefix
// before observer settings is also an explicitly supported v1 payload: it was
// used before the observer fields were appended.
struct MQTTPrefs {
  char mqtt_origin[32];
  char mqtt_iata[8];
  uint8_t mqtt_status_enabled;
  uint8_t mqtt_packets_enabled;
  uint8_t mqtt_raw_enabled;
  uint8_t mqtt_tx_enabled;
  uint32_t mqtt_status_interval;

  char wifi_ssid[32];
  char wifi_password[64];
  uint8_t wifi_power_save;

  char timezone_string[32];
  int8_t timezone_offset;

  char mqtt_slot_preset[MQTT_PREFS_SLOT_COUNT][24];
  char mqtt_slot_host[MQTT_PREFS_SLOT_COUNT][64];
  uint16_t mqtt_slot_port[MQTT_PREFS_SLOT_COUNT];
  char mqtt_slot_username[MQTT_PREFS_SLOT_COUNT][32];
  char mqtt_slot_password[MQTT_PREFS_SLOT_COUNT][64];

  char mqtt_owner_public_key[65];
  char mqtt_email[64];

  char mqtt_slot_token[MQTT_PREFS_SLOT_COUNT][48];
  char mqtt_slot_topic[MQTT_PREFS_SLOT_COUNT][96];
  char mqtt_slot_audience[MQTT_PREFS_SLOT_COUNT][64];

  uint8_t mqtt_rx_enabled;
  char mqtt_ntp_server[64];

  uint8_t snmp_enabled;
  char snmp_community[24];
  uint8_t radio_watchdog_minutes;
  uint8_t alert_enabled;
  char alert_psk_hex[33];
  uint16_t alert_wifi_minutes;
  uint16_t alert_mqtt_minutes;
  uint16_t alert_min_interval_min;
  char alert_hashtag[24];
  char alert_region[31];

  // Neighbors publishing (PSRAM boards only). Appended at the end of the
  // observer tail so a shorter (pre-neighbors) /mqtt_prefs payload from earlier
  // firmware still loads with these defaulting off/24h; keeps the format at
  // VERSION 1. Field order and sizes are kept byte-identical to the flex
  // neighbors build so a /mqtt_prefs written by either firmware is
  // interchangeable (see the offsetof static_asserts below).
  uint8_t mqtt_neighbors_enabled;
  uint32_t mqtt_neighbors_interval;

  // Per-slot payload-type allow masks. Bit N controls MeshCore packet type N
  // for both packets and raw MQTT topics. Appended so older v1 payloads load
  // with the default all-types masks intact.
  uint16_t mqtt_slot_packet_filter[MQTT_PREFS_SLOT_COUNT];
};

// Neighbor discovery is scheduled with the wrap-safe millis() helpers, whose
// signed-delta comparison requires intervals below INT32_MAX ms. The 336h
// (two-week) cap stays comfortably inside that range.
static const uint32_t MQTT_NEIGHBORS_MIN_INTERVAL_HOURS = 12;
static const uint32_t MQTT_NEIGHBORS_MAX_INTERVAL_HOURS = 336;
static const uint32_t MQTT_NEIGHBORS_DEFAULT_INTERVAL_HOURS = 24;
static const uint32_t MQTT_NEIGHBORS_MIN_INTERVAL_MS = MQTT_NEIGHBORS_MIN_INTERVAL_HOURS * 3600000UL;
static const uint32_t MQTT_NEIGHBORS_MAX_INTERVAL_MS = MQTT_NEIGHBORS_MAX_INTERVAL_HOURS * 3600000UL;
static const uint32_t MQTT_NEIGHBORS_DEFAULT_INTERVAL_MS = MQTT_NEIGHBORS_DEFAULT_INTERVAL_HOURS * 3600000UL;

// Version-1 has four payload layouts this firmware can decode. Never infer a
// compatible payload from an arbitrary SHORTER size: raw prefs have no
// checksum, so a short length has to match a boundary that was really shipped.
//
// A LONGER v1 payload is different and is always readable: within a version tag
// the layout is append-only, so a later build's file still starts with this
// binary's exact baseline. classify() reads that prefix and ignores the tail
// rather than rejecting the file — see the downgrade contract there. Any change
// that is not a pure append MUST bump MQTT_PREFS_VERSION instead.
//   - PRE_OBSERVER  (2736): stops before the observer tail (snmp_*/alert_*).
//   - PRE_NEIGHBORS (2860): full observer tail, no neighbors fields yet.
//   - PRE_FILTER    (2864): neighbors tail, no per-slot packet filters.
//   - FULL          (2876): current baseline, with six uint16_t filter masks.
//
// FULL is the maximum written, not the default: MQTTPrefsCodec::payloadLenFor()
// keeps emitting PRE_FILTER while every slot holds the all-types default, so a
// node that never touches a filter stays readable by pre-filter firmware. See
// the rollback note there — /mqtt_prefs also carries the WiFi credentials.
static const size_t MQTT_PREFS_V1_PRE_OBSERVER_PAYLOAD_SIZE = 2736;
static const size_t MQTT_PREFS_V1_PRE_NEIGHBORS_PAYLOAD_SIZE = 2860;
static const size_t MQTT_PREFS_V1_PRE_FILTER_PAYLOAD_SIZE = 2864;
static const size_t MQTT_PREFS_V1_FULL_PAYLOAD_SIZE = 2876;

// /mqtt_prefs starts with a self-describing 8-byte header. Headerless files
// are deployed legacy layouts and continue to be distinguished by size.
static const uint8_t MQTT_PREFS_MAGIC[4] = {0xF5, 'M', 'Q', 'P'};
static const uint16_t MQTT_PREFS_VERSION = 1;

struct MQTTPrefsHeader {
  uint8_t magic[4];
  uint16_t version;
  uint16_t payload_len;
};

// 3-slot MQTTPrefs layout. Array dimensions changed in the current format, so
// it must be field-copied rather than read into MQTTPrefs directly.
struct ThreeSlotMQTTPrefs {
  char mqtt_origin[32];
  char mqtt_iata[8];
  uint8_t mqtt_status_enabled;
  uint8_t mqtt_packets_enabled;
  uint8_t mqtt_raw_enabled;
  uint8_t mqtt_tx_enabled;
  uint32_t mqtt_status_interval;
  char wifi_ssid[32];
  char wifi_password[64];
  uint8_t wifi_power_save;
  char timezone_string[32];
  int8_t timezone_offset;
  char mqtt_slot_preset[3][24];
  char mqtt_slot_host[3][64];
  uint16_t mqtt_slot_port[3];
  char mqtt_slot_username[3][32];
  char mqtt_slot_password[3][64];
  char mqtt_owner_public_key[65];
  char mqtt_email[64];
  uint8_t _legacy_analyzer_us_enabled;
  uint8_t _legacy_analyzer_eu_enabled;
  char _legacy_mqtt_server[64];
  uint16_t _legacy_mqtt_port;
  char _legacy_mqtt_username[32];
  char _legacy_mqtt_password[64];
  char mqtt_slot_token[3][48];
  char mqtt_slot_topic[3][96];
};

// The earlier 3-slot format preceded token/topic fields.
struct ThreeSlotBaseMQTTPrefs {
  char mqtt_origin[32];
  char mqtt_iata[8];
  uint8_t mqtt_status_enabled;
  uint8_t mqtt_packets_enabled;
  uint8_t mqtt_raw_enabled;
  uint8_t mqtt_tx_enabled;
  uint32_t mqtt_status_interval;
  char wifi_ssid[32];
  char wifi_password[64];
  uint8_t wifi_power_save;
  char timezone_string[32];
  int8_t timezone_offset;
  char mqtt_slot_preset[3][24];
  char mqtt_slot_host[3][64];
  uint16_t mqtt_slot_port[3];
  char mqtt_slot_username[3][32];
  char mqtt_slot_password[3][64];
  char mqtt_owner_public_key[65];
  char mqtt_email[64];
  uint8_t _legacy_analyzer_us_enabled;
  uint8_t _legacy_analyzer_eu_enabled;
  char _legacy_mqtt_server[64];
  uint16_t _legacy_mqtt_port;
  char _legacy_mqtt_username[32];
  char _legacy_mqtt_password[64];
};

// Headerless 6-slot layout shipped to the deployed flex fleet. It retains the
// removed `_legacy_*` block, so it too is field-copied into MQTTPrefs.
struct Legacy6SlotMQTTPrefs {
  char mqtt_origin[32];
  char mqtt_iata[8];
  uint8_t mqtt_status_enabled;
  uint8_t mqtt_packets_enabled;
  uint8_t mqtt_raw_enabled;
  uint8_t mqtt_tx_enabled;
  uint32_t mqtt_status_interval;
  char wifi_ssid[32];
  char wifi_password[64];
  uint8_t wifi_power_save;
  char timezone_string[32];
  int8_t timezone_offset;
  char mqtt_slot_preset[MQTT_PREFS_SLOT_COUNT][24];
  char mqtt_slot_host[MQTT_PREFS_SLOT_COUNT][64];
  uint16_t mqtt_slot_port[MQTT_PREFS_SLOT_COUNT];
  char mqtt_slot_username[MQTT_PREFS_SLOT_COUNT][32];
  char mqtt_slot_password[MQTT_PREFS_SLOT_COUNT][64];
  char mqtt_owner_public_key[65];
  char mqtt_email[64];
  uint8_t _legacy_analyzer_us_enabled;
  uint8_t _legacy_analyzer_eu_enabled;
  char _legacy_mqtt_server[64];
  uint16_t _legacy_mqtt_port;
  char _legacy_mqtt_username[32];
  char _legacy_mqtt_password[64];
  char mqtt_slot_token[MQTT_PREFS_SLOT_COUNT][48];
  char mqtt_slot_topic[MQTT_PREFS_SLOT_COUNT][96];
  char mqtt_slot_audience[MQTT_PREFS_SLOT_COUNT][64];
  uint8_t mqtt_rx_enabled;
  char mqtt_ntp_server[64];
};

// Historical headerless 6-slot variants. They share a common prefix but only
// later files contain the appended audience, RX, and NTP fields.
static const size_t LEGACY6_BASE_SIZE = 2452;
static const size_t LEGACY6_AUDIENCE_SIZE = 2836;
static const size_t LEGACY6_AUDIENCE_RX_SIZE = 2840;

// Frozen on-flash layouts; every firmware and native fixture build checks them.
static_assert(sizeof(MQTTPrefsHeader) == 8, "versioned /mqtt_prefs header must stay 8 bytes");
static_assert(offsetof(MQTTPrefs, snmp_enabled) == MQTT_PREFS_V1_PRE_OBSERVER_PAYLOAD_SIZE,
              "v1 pre-observer /mqtt_prefs boundary changed");
static_assert(sizeof(MQTTPrefs) == MQTT_PREFS_V1_FULL_PAYLOAD_SIZE,
              "v1 /mqtt_prefs payload layout changed");
// Lock the neighbors tail to the flex neighbors build's layout so a /mqtt_prefs
// written by either firmware is byte-for-byte interchangeable. The enable flag
// lands in the old struct's zeroed trailing padding (offset 2857), and the
// interval begins exactly at the pre-neighbors payload size (2860) so a
// pre-neighbors read stops right before it and the interval keeps its default.
static_assert(offsetof(MQTTPrefs, mqtt_neighbors_enabled) == 2857,
              "neighbors enable flag must sit at the flex-compatible offset");
static_assert(offsetof(MQTTPrefs, mqtt_neighbors_interval) == MQTT_PREFS_V1_PRE_NEIGHBORS_PAYLOAD_SIZE,
              "neighbors interval offset must equal the pre-neighbors payload size");
static_assert(offsetof(MQTTPrefs, mqtt_slot_packet_filter) == MQTT_PREFS_V1_PRE_FILTER_PAYLOAD_SIZE,
              "packet filters must begin at the pre-filter payload boundary");
static_assert(sizeof(OldMQTTPrefs) == 472, "frozen pre-slot /mqtt_prefs layout changed");
static_assert(sizeof(PreWifiPowerOldMQTTPrefs) == 472, "frozen pre-WiFi-power /mqtt_prefs layout changed");
static_assert(offsetof(OldMQTTPrefs, wifi_power_save) == 144,
              "frozen post-WiFi-power discriminator offset changed");
static_assert(offsetof(OldMQTTPrefs, timezone_string) == 145,
              "frozen post-WiFi-power timezone offset changed");
static_assert(offsetof(OldMQTTPrefs, timezone_offset) == 177,
              "frozen post-WiFi-power UTC offset changed");
static_assert(offsetof(OldMQTTPrefs, mqtt_server) == 178,
              "frozen post-WiFi-power server offset changed");
static_assert(offsetof(PreWifiPowerOldMQTTPrefs, timezone_string) == 144,
              "frozen pre-WiFi-power timezone offset changed");
static_assert(offsetof(PreWifiPowerOldMQTTPrefs, timezone_offset) == 176,
              "frozen pre-WiFi-power UTC offset changed");
static_assert(offsetof(PreWifiPowerOldMQTTPrefs, mqtt_server) == 177,
              "frozen pre-WiFi-power server offset changed");
static_assert(sizeof(ThreeSlotBaseMQTTPrefs) == 1032, "frozen early 3-slot /mqtt_prefs layout changed");
static_assert(sizeof(ThreeSlotMQTTPrefs) == 1464, "frozen 3-slot /mqtt_prefs layout changed");
static_assert(offsetof(ThreeSlotMQTTPrefs, mqtt_slot_token) == 1030,
              "frozen 3-slot token offset changed");
static_assert(offsetof(ThreeSlotMQTTPrefs, mqtt_slot_topic) == 1174,
              "frozen 3-slot topic offset changed");
static_assert(offsetof(Legacy6SlotMQTTPrefs, mqtt_slot_audience) == LEGACY6_BASE_SIZE,
              "frozen early 6-slot /mqtt_prefs prefix changed");
static_assert(offsetof(Legacy6SlotMQTTPrefs, mqtt_rx_enabled) == LEGACY6_AUDIENCE_SIZE,
              "frozen audience 6-slot /mqtt_prefs prefix changed");
static_assert(offsetof(Legacy6SlotMQTTPrefs, mqtt_ntp_server) == 2837,
              "frozen RX 6-slot /mqtt_prefs prefix changed");
static_assert(sizeof(Legacy6SlotMQTTPrefs) == 2904, "frozen deployed-fleet /mqtt_prefs layout changed");

#endif  // WITH_MQTT_BRIDGE
