// MQTT-only translation unit. 22 variants re-glob helpers/*.cpp past the
// arduino_base exclusion, so the contents are guarded here rather than in
// the build filter — same idiom as helpers/esp32/WebConfigServer.cpp.
#ifdef WITH_MQTT_BRIDGE

#include "MQTTMessageBuilder.h"
#include "MQTTPayloadBuilder.h"
#include <ArduinoJson.h>
#include <cstring>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <Timezone.h>
#include "MeshCore.h"

void MQTTMessageBuilder::formatIsoTimestampForMqtt(time_t now, long usec, Timezone* timezone, char* buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) return;
  // Always emit UTC with an explicit "+00:00" offset, matching Python's
  // datetime.now(timezone.utc).isoformat(). The system clock is UTC (SNTP offset 0),
  // so gmtime() is correct regardless of the prefs Timezone (now unused here).
  (void)timezone;
  // Clamp the sub-second to a valid microsecond range so the "%06ld" field can never
  // overflow to 7 digits or go negative on a bad clock read.
  if (usec < 0) usec = 0;
  else if (usec > 999999) usec = 999999;
  struct tm* tm_info = gmtime(&now);
  if (tm_info) {
    size_t n = strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S", tm_info);
    if (n > 0 && snprintf(buffer + n, buffer_size - n, ".%06ld+00:00", usec) > 0) {
      return;
    }
  }
  strncpy(buffer, "2024-01-01T12:00:00.000000+00:00", buffer_size - 1);
  buffer[buffer_size - 1] = '\0';
}

int MQTTMessageBuilder::buildStatusMessage(
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
  return MQTTPayloadBuilder::buildStatusMessage(
      doc, origin, origin_id, model, firmware_version, radio, client_version,
      status, timestamp, buffer, buffer_size, battery_mv, uptime_secs, errors,
      queue_len, noise_floor, tx_air_secs, rx_air_secs, recv_errors, internal_heap,
      packets_sent, packets_received, repeat);
}

int MQTTMessageBuilder::buildPacketMessage(
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
  char* buffer,
  size_t buffer_size
) {
  return MQTTPayloadBuilder::buildPacketMessage(
      doc, origin, origin_id, timestamp, direction, time, date, len, packet_type,
      route, payload_len, raw, snr, rssi, score, hash, path_bytes, path_hop_count,
      path_hash_size, MAX_PATH_SIZE, buffer, buffer_size);
}

int MQTTMessageBuilder::buildRawMessage(
  JsonDocument& doc,
  const char* origin,
  const char* origin_id,
  const char* timestamp,
  const char* raw,
  char* buffer,
  size_t buffer_size
) {
  return MQTTPayloadBuilder::buildRawMessage(
      doc, origin, origin_id, timestamp, raw, buffer, buffer_size);
}

int MQTTMessageBuilder::buildNeighborsMessage(
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
  return MQTTPayloadBuilder::buildNeighborsMessage(
      doc, origin, origin_id, timestamp, self_scopes, self_default_scope,
      neighbors, neighbor_count, buffer, buffer_size, total_neighbors,
      queried_neighbors, truncated);
}

size_t MQTTMessageBuilder::measureNeighborsMessageBase(
  const char* origin,
  const char* origin_id,
  const char* timestamp,
  const char* self_scopes,
  const char* self_default_scope,
  int total_neighbors
) {
  return MQTTPayloadBuilder::measureNeighborsMessageBase(
      origin, origin_id, timestamp, self_scopes, self_default_scope,
      total_neighbors);
}

size_t MQTTMessageBuilder::measureNeighborsMessageEntry(
  const NeighborsMessageEntry& neighbor
) {
  return MQTTPayloadBuilder::measureNeighborsMessageEntry(neighbor);
}

int MQTTMessageBuilder::buildPacketJSON(
  JsonDocument& doc,
  mesh::Packet* packet,
  bool is_tx,
  const char* origin,
  const char* origin_id,
  Timezone* timezone,
  char* buffer,
  size_t buffer_size
) {
  if (!packet) return 0;
  
  // One wall-clock read: tv_sec feeds both the timestamp and the UTC time/date
  // fields below (kept consistent), tv_usec is the real sub-second.
  struct timeval now_tv;
  gettimeofday(&now_tv, nullptr);
  time_t now = now_tv.tv_sec;
  char timestamp[40];
  formatIsoTimestampForMqtt(now, now_tv.tv_usec, timezone, timestamp, sizeof(timestamp));
  
  // Packet time/date: UTC (gmtime), same family as meshcoretomqtt serial fields
  struct tm* utc_timeinfo = gmtime(&now);
  
  // Format time and date (ALWAYS UTC)
  char time_str[16];
  char date_str[16];
  if (utc_timeinfo) {
    strftime(time_str, sizeof(time_str), "%H:%M:%S", utc_timeinfo);
    strftime(date_str, sizeof(date_str), "%d/%m/%Y", utc_timeinfo);
  } else {
    strcpy(time_str, "12:00:00");
    strcpy(date_str, "01/01/2024");
  }
  
  // Convert packet to hex
  char raw_hex[WIRE_HEX_SCRATCH_SIZE];
  packetToHex(packet, raw_hex, sizeof(raw_hex));

  // Get packet characteristics
  int packet_type = packet->getPayloadType();
  const char* route_str = getRouteTypeString(packet->isRouteDirect() ? 1 : 0);
  
  // Create proper packet hash using MeshCore's calculatePacketHash method
  char hash_str[17];
  uint8_t packet_hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(packet_hash);
  bytesToHex(packet_hash, MAX_HASH_SIZE, hash_str, sizeof(hash_str));
  
  // Routing path (direct packets only): pass raw hop bytes to buildPacketMessage,
  // which emits them as an array of lowercase hex hop tokens.
  bool has_path = packet->isRouteDirect() && packet->getPathHashCount() > 0;
  
  return buildPacketMessage(
    doc,
    origin, origin_id, timestamp,
    is_tx ? "tx" : "rx",
    time_str, date_str,
    packet->getRawLength(),
    packet_type, route_str,
    packet->payload_len,
    raw_hex,
    12.5f, // SNR - using reasonable default
    -65,   // RSSI - using reasonable default
    NAN,   // score - unknown on this reconstruction-less fallback path
    hash_str,
    has_path ? packet->path : nullptr,
    has_path ? packet->getPathHashCount() : 0,
    has_path ? packet->getPathHashSize() : 0,
    buffer, buffer_size
  );
}

int MQTTMessageBuilder::buildPacketJSONFromRaw(
  JsonDocument& doc,
  const uint8_t* raw_data,
  int raw_len,
  mesh::Packet* packet,
  bool is_tx,
  const char* origin,
  const char* origin_id,
  float snr,
  float rssi,
  float score,
  Timezone* timezone,
  char* buffer,
  size_t buffer_size
) {
  if (!packet || !raw_data || raw_len <= 0) return 0;
  
  // One wall-clock read: tv_sec feeds both the timestamp and the UTC time/date
  // fields below (kept consistent), tv_usec is the real sub-second.
  struct timeval now_tv;
  gettimeofday(&now_tv, nullptr);
  time_t now = now_tv.tv_sec;
  char timestamp[40];
  formatIsoTimestampForMqtt(now, now_tv.tv_usec, timezone, timestamp, sizeof(timestamp));
  
  struct tm* utc_timeinfo = gmtime(&now);
  
  // Format time and date (ALWAYS UTC)
  char time_str[16];
  char date_str[16];
  if (utc_timeinfo) {
    strftime(time_str, sizeof(time_str), "%H:%M:%S", utc_timeinfo);
    strftime(date_str, sizeof(date_str), "%d/%m/%Y", utc_timeinfo);
  } else {
    strcpy(time_str, "12:00:00");
    strcpy(date_str, "01/01/2024");
  }
  
  // Convert raw radio data to hex (this includes radio headers). bytesToHex() emits
  // an empty string rather than truncating if raw_len exceeds the protocol maximum.
  char raw_hex[WIRE_HEX_SCRATCH_SIZE];
  bytesToHex(raw_data, raw_len, raw_hex, sizeof(raw_hex));
  
  // Get packet characteristics from the parsed packet
  int packet_type = packet->getPayloadType();
  const char* route_str = getRouteTypeString(packet->isRouteDirect() ? 1 : 0);
  
  // Create proper packet hash using MeshCore's calculatePacketHash method
  char hash_str[17];
  uint8_t packet_hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(packet_hash);
  bytesToHex(packet_hash, MAX_HASH_SIZE, hash_str, sizeof(hash_str));
  
  // Routing path (direct packets only): pass raw hop bytes to buildPacketMessage,
  // which emits them as an array of lowercase hex hop tokens.
  bool has_path = packet->isRouteDirect() && packet->getPathHashCount() > 0;
  
  return buildPacketMessage(
    doc,
    origin, origin_id, timestamp,
    is_tx ? "tx" : "rx",
    time_str, date_str,
    raw_len, // Use actual raw radio data length
    packet_type, route_str,
    packet->payload_len,
    raw_hex,
    snr,  // Use actual SNR from radio
    rssi, // Use actual RSSI from radio
    score, // Firmware rebroadcast score (NaN for tx / when unavailable)
    hash_str,
    has_path ? packet->path : nullptr,
    has_path ? packet->getPathHashCount() : 0,
    has_path ? packet->getPathHashSize() : 0,
    buffer, buffer_size
  );
}

int MQTTMessageBuilder::buildRawJSON(
  JsonDocument& doc,
  mesh::Packet* packet,
  const char* origin,
  const char* origin_id,
  Timezone* timezone,
  char* buffer,
  size_t buffer_size
) {
  if (!packet) return 0;

  // One wall-clock read: tv_sec for the timestamp, tv_usec for the real sub-second.
  struct timeval now_tv;
  gettimeofday(&now_tv, nullptr);
  char timestamp[40];
  formatIsoTimestampForMqtt(now_tv.tv_sec, now_tv.tv_usec, timezone, timestamp, sizeof(timestamp));

  // Convert packet to hex
  char raw_hex[WIRE_HEX_SCRATCH_SIZE];
  packetToHex(packet, raw_hex, sizeof(raw_hex));

  return buildRawMessage(doc, origin, origin_id, timestamp, raw_hex, buffer, buffer_size);
}

const char* MQTTMessageBuilder::getRouteTypeString(int route_type) {
  switch (route_type) {
    case 0: return "F"; // FLOOD
    case 1: return "D"; // DIRECT
    case 2: return "T"; // TRANSPORT_DIRECT
    default: return "U"; // UNKNOWN
  }
}

void MQTTMessageBuilder::bytesToHex(const uint8_t* data, size_t len, char* hex, size_t hex_size) {
  if (hex == nullptr || hex_size == 0) return;
  // Guarantee a valid (empty) string even if we bail out below, so a caller's
  // uninitialized stack buffer is never serialized into the JSON raw/hash fields
  // when the buffer is too small (A6).
  hex[0] = '\0';
  if (hex_size < len * 2 + 1) return;

  // Nibble lookup instead of a per-byte snprintf("%02X"): same uppercase hex
  // output, but avoids re-parsing the format string up to ~512 times per publish.
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  for (size_t i = 0; i < len; i++) {
    hex[i * 2]     = HEX_DIGITS[data[i] >> 4];
    hex[i * 2 + 1] = HEX_DIGITS[data[i] & 0x0F];
  }
  hex[len * 2] = '\0';
}

void MQTTMessageBuilder::packetToHex(mesh::Packet* packet, char* hex, size_t hex_size) {
  if (hex == nullptr || hex_size == 0) return;
  // Empty string on any early-out below (serialization returned nothing, or the
  // hex buffer is too small) so an uninitialized raw_hex[] never reaches the
  // published JSON (A6).
  hex[0] = '\0';
  // Serialize full on-air/wire format using Packet::writeTo()
  // This includes header, transport codes (if present), path_len, path, and payload
  uint8_t raw_buf[WIRE_SCRATCH_SIZE];
  if (!canSerializePacket(packet, sizeof(raw_buf))) return;
  uint8_t raw_len = packet->writeTo(raw_buf);
  if (raw_len == 0) return;
  
  // Check if hex buffer is large enough (2 hex chars per byte + null terminator)
  if (hex_size < (size_t)raw_len * 2 + 1) return;
  
  // Convert serialized packet to hex
  bytesToHex(raw_buf, raw_len, hex, hex_size);
}

#endif  // WITH_MQTT_BRIDGE
