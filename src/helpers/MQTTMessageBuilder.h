#pragma once

#include "MeshCore.h"
#include <ArduinoJson.h>
#include <Mesh.h>
#include <Timezone.h>

/**
 * @brief Utility class for building MQTT JSON messages
 *
 * This class handles the formatting of mesh packets and device status
 * into JSON messages for MQTT publishing according to the MeshCore
 * packet capture specification.
 */
class MQTTMessageBuilder {
private:
  static const int JSON_BUFFER_SIZE = 1024;
  
public:
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
   * @param timestamp ISO 8601 timestamp
   * @param buffer Output buffer for JSON string
   * @param buffer_size Size of output buffer
   * @param battery_mv Battery voltage in millivolts (optional, -1 to omit)
   * @param uptime_secs Uptime in seconds (optional, -1 to omit)
   * @param errors Error flags (optional, -1 to omit)
   * @param queue_len Queue length (optional, -1 to omit)
   * @param noise_floor Noise floor in dBm (optional, -999 to omit)
   * @param tx_air_secs TX air time in seconds (optional, -1 to omit)
   * @param rx_air_secs RX air time in seconds (optional, -1 to omit)
   * @return Length of JSON string, or 0 on error
   */
  static int buildStatusMessage(
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
    int rx_air_secs = -1
  );

  /**
   * Build packet message JSON
   *
   * @param origin Device name
   * @param origin_id Device public key (hex string)
   * @param timestamp ISO 8601 timestamp
   * @param direction Packet direction ("rx" or "tx")
   * @param time Time in HH:MM:SS format
   * @param date Date in DD/MM/YYYY format
   * @param len Total packet length
   * @param packet_type Packet type code
   * @param route Routing type
   * @param payload_len Payload length
   * @param raw Raw packet data (hex string)
   * @param snr Signal-to-noise ratio
   * @param rssi Received signal strength
   * @param hash Packet hash
   * @param path Routing path (for direct packets)
   * @param buffer Output buffer for JSON string
   * @param buffer_size Size of output buffer
   * @return Length of JSON string, or 0 on error
   */
  static int buildPacketMessage(
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
    const char* hash,
    const char* path,
    char* buffer,
    size_t buffer_size
  );

  /**
   * Build raw message JSON
   *
   * @param origin Device name
   * @param origin_id Device public key (hex string)
   * @param timestamp ISO 8601 timestamp
   * @param raw Raw packet data (hex string)
   * @param buffer Output buffer for JSON string
   * @param buffer_size Size of output buffer
   * @return Length of JSON string, or 0 on error
   */
  static int buildRawMessage(
    const char* origin,
    const char* origin_id,
    const char* timestamp,
    const char* raw,
    char* buffer,
    size_t buffer_size
  );

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
    mesh::Packet* packet,
    bool is_tx,
    const char* origin,
    const char* origin_id,
    Timezone* timezone,
    char* buffer,
    size_t buffer_size
  );

  static int buildPacketJSONFromRaw(
    const uint8_t* raw_data,
    int raw_len,
    mesh::Packet* packet,
    bool is_tx,
    const char* origin,
    const char* origin_id,
    float snr,
    float rssi,
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
    mesh::Packet* packet,
    const char* origin,
    const char* origin_id,
    Timezone* timezone,
    char* buffer,
    size_t buffer_size
  );

private:
  /**
   * Convert packet type to string
   */
  static const char* getPacketTypeString(int packet_type);

  /**
   * Convert route type to string
   */
  static const char* getRouteTypeString(int route_type);

  /**
   * Format timestamp to ISO 8601 format
   */
  static void formatTimestamp(unsigned long timestamp, char* buffer, size_t buffer_size);

  /**
   * Format time to HH:MM:SS format
   */
  static void formatTime(unsigned long timestamp, char* buffer, size_t buffer_size);

  /**
   * Format date to DD/MM/YYYY format
   */
  static void formatDate(unsigned long timestamp, char* buffer, size_t buffer_size);

  /**
   * Convert bytes to hex string (uppercase)
   */
  static void bytesToHex(const uint8_t* data, size_t len, char* hex, size_t hex_size);

  /**
   * Convert packet to hex string
   */
  static void packetToHex(mesh::Packet* packet, char* hex, size_t hex_size);
};
