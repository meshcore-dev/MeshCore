#include "MQTTPayloadBuilder.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

static int serializeComplete(JsonObject root, char* buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) return 0;

  size_t written = serializeJson(root, buffer, buffer_size);
  // Preserve MQTTMessageBuilder's existing success criterion while clearing
  // ArduinoJson's truncated prefix on failure. Callers publish only a positive
  // return value, and now a failed buffer cannot be mistaken for complete JSON.
  if (written == 0 || written >= buffer_size) {
    buffer[0] = '\0';
    return 0;
  }
  return static_cast<int>(written);
}

}  // namespace

int MQTTPayloadBuilder::buildStatusMessage(
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
  int battery_mv,
  int uptime_secs,
  int errors,
  int queue_len,
  int noise_floor,
  int tx_air_secs,
  int rx_air_secs,
  int recv_errors,
  int internal_heap,
  int packets_sent,
  int packets_received,
  const char* repeat
) {
  doc.clear();
  JsonObject root = doc.to<JsonObject>();

  root["status"] = status;
  root["timestamp"] = timestamp;
  root["origin"] = origin;
  root["origin_id"] = origin_id;
  root["model"] = model;
  root["firmware_version"] = firmware_version;
  root["radio"] = radio;
  root["client_version"] = client_version;
  if (repeat != nullptr) {
    root["repeat"] = repeat;
  }

  if (battery_mv >= 0 || uptime_secs >= 0 || errors >= 0 || queue_len >= 0 ||
      noise_floor > -999 || tx_air_secs >= 0 || rx_air_secs >= 0 || recv_errors >= 0 ||
      internal_heap >= 0 || packets_sent >= 0 || packets_received >= 0) {
    JsonObject stats = root["stats"].to<JsonObject>();

    if (battery_mv >= 0) stats["battery_mv"] = battery_mv;
    if (uptime_secs >= 0) stats["uptime_secs"] = uptime_secs;
    if (packets_sent >= 0) stats["packets_sent"] = packets_sent;
    if (packets_received >= 0) stats["packets_received"] = packets_received;
    if (errors >= 0) stats["errors"] = errors;
    if (queue_len >= 0) stats["queue_len"] = queue_len;
    if (noise_floor > -999) stats["noise_floor"] = noise_floor;
    if (tx_air_secs >= 0) stats["tx_air_secs"] = tx_air_secs;
    if (rx_air_secs >= 0) stats["rx_air_secs"] = rx_air_secs;
    if (recv_errors >= 0) stats["recv_errors"] = recv_errors;
    if (internal_heap >= 0) stats["internal_heap"] = internal_heap;
  }

  return serializeComplete(root, buffer, buffer_size);
}

int MQTTPayloadBuilder::buildPacketMessage(
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
) {
  doc.clear();
  JsonObject root = doc.to<JsonObject>();

  char len_str[16];
  char packet_type_str[16];
  char payload_len_str[16];
  char snr_str[16];
  char rssi_str[16];
  char score_str[16];

  snprintf(len_str, sizeof(len_str), "%d", len);
  snprintf(packet_type_str, sizeof(packet_type_str), "%d", packet_type);
  snprintf(payload_len_str, sizeof(payload_len_str), "%d", payload_len);
  snprintf(snr_str, sizeof(snr_str), "%.1f", snr);
  snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);

  root["timestamp"] = timestamp;
  root["hash"] = hash;
  root["origin"] = origin;
  root["type"] = "PACKET";
  root["direction"] = direction;
  root["time"] = time;
  root["date"] = date;
  root["len"] = len_str;
  root["packet_type"] = packet_type_str;
  root["route"] = route;
  root["payload_len"] = payload_len_str;
  root["raw"] = raw;
  root["origin_id"] = origin_id;

  if (direction && strcmp(direction, "rx") == 0) {
    root["SNR"] = snr_str;
    root["RSSI"] = rssi_str;
    if (!isnan(score)) {
      snprintf(score_str, sizeof(score_str), "%d", static_cast<int>(score * 1000));
      root["score"] = score_str;
    }
  }

  if (path_bytes && path_hop_count > 0 && path_hash_size > 0) {
    JsonArray path_arr = root["path"].to<JsonArray>();
    char hop_hex[2 * 4 + 1];
    for (int i = 0; i < path_hop_count; i++) {
      size_t pos = 0;
      for (int b = 0; b < path_hash_size && b < 4; b++) {
        size_t idx = static_cast<size_t>(i) * path_hash_size + b;
        if (idx >= max_path_bytes) break;
        snprintf(hop_hex + pos, 3, "%02x", path_bytes[idx]);
        pos += 2;
      }
      hop_hex[pos] = '\0';
      path_arr.add(hop_hex);
    }
  }

  return serializeComplete(root, buffer, buffer_size);
}

int MQTTPayloadBuilder::buildRawMessage(
  JsonDocument& doc,
  const char* origin,
  const char* origin_id,
  const char* timestamp,
  const char* raw,
  char* buffer,
  size_t buffer_size
) {
  doc.clear();
  JsonObject root = doc.to<JsonObject>();

  root["origin"] = origin;
  root["origin_id"] = origin_id;
  root["timestamp"] = timestamp;
  root["type"] = "RAW";
  root["data"] = raw;

  return serializeComplete(root, buffer, buffer_size);
}

static JsonArray buildNeighborsMessageBase(
  JsonDocument& doc,
  const char* origin,
  const char* origin_id,
  const char* timestamp,
  const char* self_scopes,
  const char* self_default_scope,
  int total_neighbors,
  int queried_neighbors,
  bool truncated
) {
  doc.clear();
  JsonObject root = doc.to<JsonObject>();
  root["timestamp"] = timestamp;
  root["origin"] = origin;
  root["origin_id"] = origin_id;
  if (total_neighbors >= 0) {
    root["total_neighbors"] = total_neighbors;
    root["queried_neighbors"] = queried_neighbors >= 0 ? queried_neighbors : total_neighbors;
    root["truncated"] = truncated;
  }

  JsonObject self = root["self"].to<JsonObject>();
  self["scopes"] = self_scopes ? self_scopes : "";
  self["default_scope"] = self_default_scope ? self_default_scope : "";
  return root["neighbors"].to<JsonArray>();
}

static void addNeighborsMessageEntry(
  JsonArray& arr,
  const MQTTPayloadBuilder::NeighborsMessageEntry& neighbor
) {
  JsonObject nb = arr.add<JsonObject>();
  nb["pubkey"] = neighbor.pubkey_hex;
  nb["snr"] = neighbor.snr;
  if (neighbor.heard_unknown) {
    nb["heard_secs_ago"] = nullptr;  // age unknown, not zero
  } else {
    nb["heard_secs_ago"] = neighbor.heard_secs_ago;
  }
  nb["scopes"] = neighbor.scopes ? neighbor.scopes : "";
  nb["status"] = neighbor.status;
}

size_t MQTTPayloadBuilder::measureNeighborsMessageBase(
  const char* origin,
  const char* origin_id,
  const char* timestamp,
  const char* self_scopes,
  const char* self_default_scope,
  int total_neighbors
) {
  JsonDocument doc;
  buildNeighborsMessageBase(
    doc, origin, origin_id, timestamp, self_scopes, self_default_scope,
    total_neighbors, total_neighbors, false);
  return measureJson(doc);
}

size_t MQTTPayloadBuilder::measureNeighborsMessageEntry(
  const NeighborsMessageEntry& neighbor
) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  addNeighborsMessageEntry(arr, neighbor);
  JsonObject entry = arr[0];
  return measureJson(entry);
}

int MQTTPayloadBuilder::buildNeighborsMessage(
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
  int total_neighbors,
  int queried_neighbors,
  bool truncated
) {
  if (!buffer || buffer_size == 0) return 0;

  JsonArray arr = buildNeighborsMessageBase(
    doc, origin, origin_id, timestamp, self_scopes, self_default_scope,
    total_neighbors, queried_neighbors, truncated);
  if (doc.overflowed() || arr.isNull() || measureJson(doc) >= buffer_size) return 0;

  for (int i = 0; i < neighbor_count; i++) {
    addNeighborsMessageEntry(arr, neighbors[i]);
    if (doc.overflowed()) return 0;

    // Entries arrive ordered most- to least-useful. Stop as soon as the next
    // one would fill the fixed publish buffer, dropping the remaining tail so
    // document growth stays bounded.
    if (measureJson(doc) >= buffer_size) {
      arr.remove(arr.size() - 1);
      if (total_neighbors >= 0) doc["truncated"] = true;
      break;
    }
  }

  return serializeComplete(doc.as<JsonObject>(), buffer, buffer_size);
}
