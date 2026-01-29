#include "MQTTMessageBuilder.h"
#include <ArduinoJson.h>
#include <time.h>
#include <Timezone.h>
#include "MeshCore.h"

int MQTTMessageBuilder::buildStatusMessage(
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
  int rx_air_secs
) {
  // Use StaticJsonDocument to avoid heap fragmentation (fixed-size stack allocation)
  StaticJsonDocument<768> doc;  // Increased size to accommodate stats
  JsonObject root = doc.to<JsonObject>();
  
  root["status"] = status;
  root["timestamp"] = timestamp;
  root["origin"] = origin;
  root["origin_id"] = origin_id;
  root["model"] = model;
  root["firmware_version"] = firmware_version;
  root["radio"] = radio;
  root["client_version"] = client_version;
  
  // Add stats object if any stats are provided
  if (battery_mv >= 0 || uptime_secs >= 0 || errors >= 0 || queue_len >= 0 || 
      noise_floor > -999 || tx_air_secs >= 0 || rx_air_secs >= 0) {
    JsonObject stats = root.createNestedObject("stats");
    
    if (battery_mv >= 0) {
      stats["battery_mv"] = battery_mv;
    }
    if (uptime_secs >= 0) {
      stats["uptime_secs"] = uptime_secs;
    }
    if (errors >= 0) {
      stats["errors"] = errors;
    }
    if (queue_len >= 0) {
      stats["queue_len"] = queue_len;
    }
    if (noise_floor > -999) {
      stats["noise_floor"] = noise_floor;
    }
    if (tx_air_secs >= 0) {
      stats["tx_air_secs"] = tx_air_secs;
    }
    if (rx_air_secs >= 0) {
      stats["rx_air_secs"] = rx_air_secs;
    }
  }
  
  size_t len = serializeJson(root, buffer, buffer_size);
  return (len > 0 && len < buffer_size) ? len : 0;
}

int MQTTMessageBuilder::buildPacketMessage(
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
) {
  // Use StaticJsonDocument with fixed maximum size to avoid heap fragmentation
  // Base JSON overhead ~200 bytes, raw hex can be up to 510 chars (255 bytes packet)
  // Use maximum size (2048) to handle all packet sizes without heap allocation
  StaticJsonDocument<2048> doc;
  JsonObject root = doc.to<JsonObject>();
  
  // Format numeric values as strings to avoid String object allocations
  char len_str[16];
  char packet_type_str[16];
  char payload_len_str[16];
  char snr_str[16];
  char rssi_str[16];
  
  snprintf(len_str, sizeof(len_str), "%d", len);
  snprintf(packet_type_str, sizeof(packet_type_str), "%d", packet_type);
  snprintf(payload_len_str, sizeof(payload_len_str), "%d", payload_len);
  snprintf(snr_str, sizeof(snr_str), "%.1f", snr);
  snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);
  
  root["origin"] = origin;
  root["origin_id"] = origin_id;
  root["timestamp"] = timestamp;
  root["type"] = "PACKET";
  root["direction"] = direction;
  root["time"] = time;
  root["date"] = date;
  root["len"] = len_str;
  root["packet_type"] = packet_type_str;
  root["route"] = route;
  root["payload_len"] = payload_len_str;
  root["raw"] = raw;
  root["SNR"] = snr_str;
  root["RSSI"] = rssi_str;
  root["hash"] = hash;
  
  if (path && strlen(path) > 0) {
    root["path"] = path;
  }
  
  size_t json_len = serializeJson(root, buffer, buffer_size);
  return (json_len > 0 && json_len < buffer_size) ? json_len : 0;
}

int MQTTMessageBuilder::buildRawMessage(
  const char* origin,
  const char* origin_id,
  const char* timestamp,
  const char* raw,
  char* buffer,
  size_t buffer_size
) {
  // Use StaticJsonDocument to avoid heap fragmentation (fixed-size stack allocation)
  StaticJsonDocument<512> doc;
  JsonObject root = doc.to<JsonObject>();
  
  root["origin"] = origin;
  root["origin_id"] = origin_id;
  root["timestamp"] = timestamp;
  root["type"] = "RAW";
  root["data"] = raw;
  
  size_t len = serializeJson(root, buffer, buffer_size);
  return (len > 0 && len < buffer_size) ? len : 0;
}

int MQTTMessageBuilder::buildPacketJSON(
  mesh::Packet* packet,
  bool is_tx,
  const char* origin,
  const char* origin_id,
  Timezone* timezone,
  char* buffer,
  size_t buffer_size
) {
  if (!packet) return 0;
  
  // Get current device time (should be UTC since system timezone is set to UTC)
  time_t now = time(nullptr);
  
  // Convert to local time using timezone library (for timestamp field only)
  time_t local_time = timezone ? timezone->toLocal(now) : now;
  struct tm* local_timeinfo = localtime(&local_time);
  
  // Format timestamp in ISO 8601 format (LOCAL TIME)
  char timestamp[32];
  if (local_timeinfo) {
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000000", local_timeinfo);
  } else {
    strcpy(timestamp, "2024-01-01T12:00:00.000000");
  }
  
  // Get UTC time (since system timezone is UTC, time() returns UTC)
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
  // MAX_TRANS_UNIT is 255 bytes, hex = 510 chars, but allow for larger with headers
  char raw_hex[1024];
  packetToHex(packet, raw_hex, sizeof(raw_hex));
  
  // Get packet characteristics
  int packet_type = packet->getPayloadType();
  const char* route_str = getRouteTypeString(packet->isRouteDirect() ? 1 : 0);
  
  // Create proper packet hash using MeshCore's calculatePacketHash method
  char hash_str[17];
  uint8_t packet_hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(packet_hash);
  bytesToHex(packet_hash, MAX_HASH_SIZE, hash_str, sizeof(hash_str));
  
  // Build path string for direct packets
  char path_str[128] = "";
  if (packet->isRouteDirect() && packet->path_len > 0) {
    // Simplified path representation
    snprintf(path_str, sizeof(path_str), "path_len_%d", packet->path_len);
  }
  
  return buildPacketMessage(
    origin, origin_id, timestamp,
    is_tx ? "tx" : "rx",
    time_str, date_str,
    packet->path_len + packet->payload_len + 2,
    packet_type, route_str,
    packet->payload_len,
    raw_hex,
    12.5f, // SNR - using reasonable default
    -65,   // RSSI - using reasonable default
    hash_str,
    packet->isRouteDirect() ? path_str : nullptr,
    buffer, buffer_size
  );
}

int MQTTMessageBuilder::buildPacketJSONFromRaw(
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
) {
  if (!packet || !raw_data || raw_len <= 0) return 0;
  
  // Get current device time (should be UTC since system timezone is set to UTC)
  time_t now = time(nullptr);
  
  // Convert to local time using timezone library (for timestamp field only)
  time_t local_time = timezone ? timezone->toLocal(now) : now;
  struct tm* local_timeinfo = localtime(&local_time);
  
  // Format timestamp in ISO 8601 format (LOCAL TIME)
  char timestamp[32];
  if (local_timeinfo) {
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000000", local_timeinfo);
  } else {
    strcpy(timestamp, "2024-01-01T12:00:00.000000");
  }
  
  // Get UTC time (since system timezone is UTC, time() returns UTC)
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
  
  // Convert raw radio data to hex (this includes radio headers)
  // MAX_TRANS_UNIT is 255 bytes, hex = 510 chars, but allow for larger with headers
  char raw_hex[1024];
  bytesToHex(raw_data, raw_len, raw_hex, sizeof(raw_hex));
  
  // Get packet characteristics from the parsed packet
  int packet_type = packet->getPayloadType();
  const char* route_str = getRouteTypeString(packet->isRouteDirect() ? 1 : 0);
  
  // Create proper packet hash using MeshCore's calculatePacketHash method
  char hash_str[17];
  uint8_t packet_hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(packet_hash);
  bytesToHex(packet_hash, MAX_HASH_SIZE, hash_str, sizeof(hash_str));
  
  // Build path string for direct packets
  char path_str[128] = "";
  if (packet->isRouteDirect() && packet->path_len > 0) {
    // Simplified path representation
    snprintf(path_str, sizeof(path_str), "path_len_%d", packet->path_len);
  }
  
  return buildPacketMessage(
    origin, origin_id, timestamp,
    is_tx ? "tx" : "rx",
    time_str, date_str,
    raw_len, // Use actual raw radio data length
    packet_type, route_str,
    packet->payload_len,
    raw_hex,
    snr,  // Use actual SNR from radio
    rssi, // Use actual RSSI from radio
    hash_str,
    packet->isRouteDirect() ? path_str : nullptr,
    buffer, buffer_size
  );
}

int MQTTMessageBuilder::buildRawJSON(
  mesh::Packet* packet,
  const char* origin,
  const char* origin_id,
  Timezone* timezone,
  char* buffer,
  size_t buffer_size
) {
  if (!packet) return 0;
  
  // Get current device time
  time_t now = time(nullptr);
  
  // Convert to local time using timezone library
  time_t local_time = timezone ? timezone->toLocal(now) : now;
  struct tm* timeinfo = localtime(&local_time);
  
  // Format timestamp in ISO 8601 format
  char timestamp[32];
  if (timeinfo) {
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000000", timeinfo);
  } else {
    strcpy(timestamp, "2024-01-01T12:00:00.000000");
  }
  
  // Convert packet to hex
  // MAX_TRANS_UNIT is 255, so max hex size is 510 chars + null = 511 bytes
  char raw_hex[1024];
  packetToHex(packet, raw_hex, sizeof(raw_hex));
  
  return buildRawMessage(origin, origin_id, timestamp, raw_hex, buffer, buffer_size);
}

const char* MQTTMessageBuilder::getPacketTypeString(int packet_type) {
  switch (packet_type) {
    case 0: return "0";   // REQ
    case 1: return "1";   // RESPONSE
    case 2: return "2";   // TXT_MSG
    case 3: return "3";   // ACK
    case 4: return "4";   // ADVERT
    case 5: return "5";   // GRP_TXT
    case 6: return "6";   // GRP_DATA
    case 7: return "7";   // ANON_REQ
    case 8: return "8";   // PATH
    case 9: return "9";   // TRACE
    case 10: return "10"; // MULTIPART
    case 11: return "11"; // Type11
    case 12: return "12"; // Type12
    case 13: return "13"; // Type13
    case 14: return "14"; // Type14
    case 15: return "15"; // RAW_CUSTOM
    default: return "0";
  }
}

const char* MQTTMessageBuilder::getRouteTypeString(int route_type) {
  switch (route_type) {
    case 0: return "F"; // FLOOD
    case 1: return "D"; // DIRECT
    case 2: return "T"; // TRANSPORT_DIRECT
    default: return "U"; // UNKNOWN
  }
}

void MQTTMessageBuilder::formatTimestamp(unsigned long timestamp, char* buffer, size_t buffer_size) {
  // Simplified timestamp formatting - in real implementation would use proper time
  snprintf(buffer, buffer_size, "2024-01-01T12:00:00.000000");
}

void MQTTMessageBuilder::formatTime(unsigned long timestamp, char* buffer, size_t buffer_size) {
  // Simplified time formatting
  snprintf(buffer, buffer_size, "12:00:00");
}

void MQTTMessageBuilder::formatDate(unsigned long timestamp, char* buffer, size_t buffer_size) {
  // Simplified date formatting
  snprintf(buffer, buffer_size, "01/01/2024");
}

void MQTTMessageBuilder::bytesToHex(const uint8_t* data, size_t len, char* hex, size_t hex_size) {
  if (hex_size < len * 2 + 1) return;
  
  for (size_t i = 0; i < len; i++) {
    snprintf(hex + i * 2, 3, "%02X", data[i]);
  }
  hex[len * 2] = '\0';
}

void MQTTMessageBuilder::packetToHex(mesh::Packet* packet, char* hex, size_t hex_size) {
  // Serialize full on-air/wire format using Packet::writeTo()
  // This includes header, transport codes (if present), path_len, path, and payload
  uint8_t raw_buf[512];
  uint8_t raw_len = packet->writeTo(raw_buf);
  if (raw_len == 0 || raw_len > sizeof(raw_buf)) return;
  
  // Check if hex buffer is large enough (2 hex chars per byte + null terminator)
  if (hex_size < (size_t)raw_len * 2 + 1) return;
  
  // Convert serialized packet to hex
  bytesToHex(raw_buf, raw_len, hex, hex_size);
}