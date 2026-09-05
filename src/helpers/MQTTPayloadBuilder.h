#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

// Mesh-independent JSON serialization core for MQTT publication payloads.
// MQTTMessageBuilder keeps the firmware-facing API and delegates these three
// deterministic contracts here so they can be exercised by native tests.
class MQTTPayloadBuilder {
public:
  static int buildStatusMessage(
    JsonDocument& doc,
    const char* origin,
    const char* origin_id,
    const char* model,
    const char* firmware_version,
    const char* radio,
    const char* client_version,
    const char* status,
    const char* timestamp,
    char* buffer,
    size_t buffer_size,
    int battery_mv = -1,
    int uptime_secs = -1,
    int errors = -1,
    int queue_len = -1,
    int noise_floor = -999,
    int tx_air_secs = -1,
    int rx_air_secs = -1,
    int recv_errors = -1,
    int internal_heap = -1,
    int packets_sent = -1,
    int packets_received = -1,
    const char* repeat = nullptr
  );

  static int buildPacketMessage(
    JsonDocument& doc,
    const char* origin,
    const char* origin_id,
    const char* timestamp,
    const char* direction,
    const char* time,
    const char* date,
    int len,
    int packet_type,
    const char* route,
    int payload_len,
    const char* raw,
    float snr,
    int rssi,
    float score,
    const char* hash,
    const uint8_t* path_bytes,
    int path_hop_count,
    int path_hash_size,
    size_t max_path_bytes,
    char* buffer,
    size_t buffer_size
  );

  static int buildRawMessage(
    JsonDocument& doc,
    const char* origin,
    const char* origin_id,
    const char* timestamp,
    const char* raw,
    char* buffer,
    size_t buffer_size
  );

  struct NeighborsMessageEntry {
    const char* pubkey_hex;
    float snr;
    uint32_t heard_secs_ago;
    const char* scopes;
    const char* status;
    // True renders heard_secs_ago as JSON null, for a neighbour whose stored
    // stamp cannot yield an age. No default initializer: the struct stays an
    // aggregate for the device toolchain, and a zeroed tail means "age known",
    // so size-measurement callers keep reserving the widest numeric value.
    bool heard_unknown;
  };

  // Build neighbors-table JSON for the meshcore/{iata}/{device}/neighbors topic.
  // Callers order entries most- to least-useful; document growth is bounded to
  // buffer_size and the remaining tail is dropped once the next entry won't fit.
  static int buildNeighborsMessage(
    JsonDocument& doc,
    const char* origin,
    const char* origin_id,
    const char* timestamp,
    const char* self_scopes,
    const char* self_default_scope,
    const NeighborsMessageEntry* neighbors,
    int neighbor_count,
    char* buffer,
    size_t buffer_size,
    int total_neighbors = -1,
    int queried_neighbors = -1,
    bool truncated = false
  );

  // Exact serialized-size components used by paced neighbor discovery. The
  // base reserves the largest progress metadata values for this snapshot;
  // callers add each entry size plus one byte for commas after the first.
  static size_t measureNeighborsMessageBase(
    const char* origin,
    const char* origin_id,
    const char* timestamp,
    const char* self_scopes,
    const char* self_default_scope,
    int total_neighbors
  );
  static size_t measureNeighborsMessageEntry(const NeighborsMessageEntry& neighbor);
};
