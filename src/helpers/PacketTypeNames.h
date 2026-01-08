#pragma once

#include <Packet.h>
#include <helpers/AdvertDataHelpers.h>

namespace mesh {

constexpr int MAX_PACKET_TYPES = 16;
constexpr int MAX_ADVERT_TYPES = 5;

// Packet type name strings - stored in flash memory
// These correspond to PAYLOAD_TYPE_* values 0x00 through 0x0F
static const char* const PACKET_TYPE_NAMES[MAX_PACKET_TYPES] = {
  "req",       // 0x00 PAYLOAD_TYPE_REQ
  "resp",      // 0x01 PAYLOAD_TYPE_RESPONSE
  "txt",       // 0x02 PAYLOAD_TYPE_TXT_MSG
  "ack",       // 0x03 PAYLOAD_TYPE_ACK
  "advert",    // 0x04 PAYLOAD_TYPE_ADVERT
  "grp.txt",   // 0x05 PAYLOAD_TYPE_GRP_TXT
  "grp.data",  // 0x06 PAYLOAD_TYPE_GRP_DATA
  "anon",      // 0x07 PAYLOAD_TYPE_ANON_REQ
  "path",      // 0x08 PAYLOAD_TYPE_PATH
  "trace",     // 0x09 PAYLOAD_TYPE_TRACE
  "multi",     // 0x0A PAYLOAD_TYPE_MULTIPART
  "control",   // 0x0B PAYLOAD_TYPE_CONTROL
  "12",        // 0x0C (reserved)
  "13",        // 0x0D (reserved)
  "14",        // 0x0E (reserved)
  "raw"        // 0x0F PAYLOAD_TYPE_RAW_CUSTOM
};

// Advert type name strings - stored in flash memory
// These correspond to ADV_TYPE_* values 0 through 4
static const char* const ADVERT_TYPE_NAMES[MAX_ADVERT_TYPES] = {
  "none",      // 0 ADV_TYPE_NONE
  "chat",      // 1 ADV_TYPE_CHAT (companion radios)
  "repeater",  // 2 ADV_TYPE_REPEATER
  "room",      // 3 ADV_TYPE_ROOM (room servers)
  "sensor"     // 4 ADV_TYPE_SENSOR
};

// Helper function to safely get packet type name
inline const char* getPacketTypeName(uint8_t type) {
  if (type >= MAX_PACKET_TYPES) return "unknown";
  return PACKET_TYPE_NAMES[type];
}

// Helper function to find packet type index by name
inline int findPacketTypeByName(const char* name) {
  for (int i = 0; i < MAX_PACKET_TYPES; i++) {
    if (strcmp(name, PACKET_TYPE_NAMES[i]) == 0) {
      return i;
    }
  }
  return -1;
}

// Helper function to safely get advert type name
inline const char* getAdvertTypeName(uint8_t type) {
  if (type >= MAX_ADVERT_TYPES) return "unknown";
  return ADVERT_TYPE_NAMES[type];
}

// Helper function to find advert type index by name
inline int findAdvertTypeByName(const char* name) {
  for (int i = 0; i < MAX_ADVERT_TYPES; i++) {
    if (strcmp(name, ADVERT_TYPE_NAMES[i]) == 0) {
      return i;
    }
  }
  return -1;
}

}
