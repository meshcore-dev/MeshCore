#pragma once

#include "MeshCore.h"
#include <ArduinoJson.h>
#include "MQTTPayloadBuilder.h"
#include "MQTTWireScratch.h"
#include <Mesh.h>
#include <Timezone.h>

/**
 * @brief Utility class for building MQTT JSON messages
 *
 * This class handles the formatting of mesh packets and device status
 * into JSON messages for MQTT publishing according to the MeshCore
 * packet capture specification.
 *
 * Timestamps in the JSON `timestamp` field are emitted in UTC with an explicit
 * "+00:00" offset (equivalent to Python datetime.now(timezone.utc).isoformat()) for
 * status, packet, and raw topics. Packet payloads also include separate `time` and
 * `date` strings in UTC (gmtime) so they stay aligned with meshcoretomqtt serial
 * regex fields.
 */
class MQTTMessageBuilder {
public:
  // Wire-format scratch sizing and validation live in the pure, host-tested
  // MQTTWireScratch; these are the firmware-facing aliases.
  static const size_t WIRE_SCRATCH_SIZE = MQTTWireScratch::kWireBytes;
  static const size_t WIRE_HEX_SCRATCH_SIZE = MQTTWireScratch::kWireHexChars;

  static bool canSerializePacket(const mesh::Packet* packet, size_t dest_size) {
    return packet != nullptr && MQTTWireScratch::canSerialize(*packet, dest_size);
  }

  /**
   * Format the MQTT JSON `timestamp` field (same rule for status, packet, raw).
   * Always UTC with an explicit "+00:00" offset, ISO-8601
   * "%Y-%m-%dT%H:%M:%S.uuuuuu+00:00" (matches Python
   * datetime.now(timezone.utc).isoformat()). The `timezone` parameter is retained
   * for API compatibility but ignored — the system clock is UTC.
   *
   * `usec` is the sub-second component in microseconds (0..999999), normally taken
   * from the same gettimeofday() read as `now` so the two don't tear at a second
   * boundary. It is a real sub-second (SNTP-maintained wall clock), not a literal;
   * pass 0 if no sub-second source is available.
   */
  static void formatIsoTimestampForMqtt(time_t now, long usec, Timezone* timezone, char* buffer, size_t buffer_size);

  /**
   * Build status message JSON
   *
   * @param origin Device name
   * @param origin_id Device public key (hex string)
   * @param model Device model
   * @param firmware_version Firmware version
   * @param radio Radio information
   * @param client_version Client version
   * @param status Connection status ("online" or "offline")
   * @param timestamp ISO-like timestamp (see formatIsoTimestampForMqtt)
   * @param buffer Output buffer for JSON string
   * @param buffer_size Size of output buffer
   * @param battery_mv Battery voltage in millivolts (optional, -1 to omit)
   * @param uptime_secs Uptime in seconds (optional, -1 to omit)
   * @param errors Error flags (optional, -1 to omit)
   * @param queue_len Queue length (optional, -1 to omit)
   * @param noise_floor Noise floor in dBm (optional, -999 to omit)
   * @param tx_air_secs TX air time in seconds (optional, -1 to omit)
   * @param rx_air_secs RX air time in seconds (optional, -1 to omit)
   * @param recv_errors Radio receive/CRC errors (optional, -1 to omit)
   * @param internal_heap Internal heap free bytes (optional, -1 to omit)
   * @param repeat Repeat/forwarding status ("on" or "off"); nullptr omits the field
   * @return Length of JSON string, or 0 on error
   */
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

  /**
   * Build packet message JSON
   *
   * @param origin Device name
   * @param origin_id Device public key (hex string)
   * @param timestamp ISO-like timestamp (see formatIsoTimestampForMqtt)
   * @param direction Packet direction ("rx" or "tx")
   * @param time Time in HH:MM:SS (UTC, gmtime; meshcoretomqtt serial parity)
   * @param date Date in DD/MM/YYYY (UTC, gmtime)
   * @param len Total packet length
   * @param packet_type Packet type code
   * @param route Routing type
   * @param payload_len Payload length
   * @param raw Raw packet data (hex string)
   * @param snr Signal-to-noise ratio
   * @param rssi Received signal strength
   * @param hash Packet hash
   * @param path_bytes Raw routing-path bytes (direct packets only; nullptr to omit)
   * @param path_hop_count Number of path hops (0 to omit the path field)
   * @param path_hash_size Bytes per hop hash (1-4)
   * @param buffer Output buffer for JSON string
   * @param buffer_size Size of output buffer
   * @return Length of JSON string, or 0 on error
   */
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
    char* buffer,
    size_t buffer_size
  );

  /**
   * Build raw message JSON
   *
   * @param origin Device name
   * @param origin_id Device public key (hex string)
   * @param timestamp ISO-like timestamp (see formatIsoTimestampForMqtt)
   * @param raw Raw packet data (hex string)
   * @param buffer Output buffer for JSON string
   * @param buffer_size Size of output buffer
   * @return Length of JSON string, or 0 on error
   */
  static int buildRawMessage(
    JsonDocument& doc,
    const char* origin,
    const char* origin_id,
    const char* timestamp,
    const char* raw,
    char* buffer,
    size_t buffer_size
  );

  // Neighbors table entry + JSON builder. The layout logic lives in the pure,
  // host-tested MQTTPayloadBuilder; this is the firmware-facing alias/delegate,
  // matching the status/packet/raw builders.
  using NeighborsMessageEntry = MQTTPayloadBuilder::NeighborsMessageEntry;
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
  static size_t measureNeighborsMessageBase(
    const char* origin,
    const char* origin_id,
    const char* timestamp,
    const char* self_scopes,
    const char* self_default_scope,
    int total_neighbors
  );
  static size_t measureNeighborsMessageEntry(const NeighborsMessageEntry& neighbor);

  /**
   * Convert packet to JSON message
   *
   * @param packet Mesh packet
   * @param is_tx Whether packet was transmitted (true) or received (false)
   * @param origin Device name
   * @param origin_id Device public key (hex string)
   * @param buffer Output buffer for JSON string
   * @param buffer_size Size of output buffer
   * @return Length of JSON string, or 0 on error
   */
  static int buildPacketJSON(
    JsonDocument& doc,
    mesh::Packet* packet,
    bool is_tx,
    const char* origin,
    const char* origin_id,
    Timezone* timezone,
    char* buffer,
    size_t buffer_size
  );

  static int buildPacketJSONFromRaw(
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
  );

  /**
   * Convert packet to raw JSON message
   *
   * @param packet Mesh packet
   * @param origin Device name
   * @param origin_id Device public key (hex string)
   * @param buffer Output buffer for JSON string
   * @param buffer_size Size of output buffer
   * @return Length of JSON string, or 0 on error
   */
  static int buildRawJSON(
    JsonDocument& doc,
    mesh::Packet* packet,
    const char* origin,
    const char* origin_id,
    Timezone* timezone,
    char* buffer,
    size_t buffer_size
  );

private:
  /**
   * Convert route type to string
   */
  static const char* getRouteTypeString(int route_type);

  /**
   * Convert bytes to hex string (uppercase)
   */
  static void bytesToHex(const uint8_t* data, size_t len, char* hex, size_t hex_size);

  /**
   * Convert packet to hex string
   */
  static void packetToHex(mesh::Packet* packet, char* hex, size_t hex_size);
};
