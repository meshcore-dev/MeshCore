#include "Packet.h"
#include <stdio.h>
#include <string.h>
#include <SHA256.h>

namespace mesh {

namespace {

struct PayloadTypeName { uint8_t type; const char* name; };

static const PayloadTypeName PAYLOAD_TYPE_NAMES[] = {
  { PAYLOAD_TYPE_REQ, "PAYLOAD_TYPE_REQ" },
  { PAYLOAD_TYPE_RESPONSE, "PAYLOAD_TYPE_RESPONSE" },
  { PAYLOAD_TYPE_TXT_MSG, "PAYLOAD_TYPE_TXT_MSG" },
  { PAYLOAD_TYPE_ACK, "PAYLOAD_TYPE_ACK" },
  { PAYLOAD_TYPE_ADVERT, "PAYLOAD_TYPE_ADVERT" },
  { PAYLOAD_TYPE_GRP_TXT, "PAYLOAD_TYPE_GRP_TXT" },
  { PAYLOAD_TYPE_GRP_DATA, "PAYLOAD_TYPE_GRP_DATA" },
  { PAYLOAD_TYPE_ANON_REQ, "PAYLOAD_TYPE_ANON_REQ" },
  { PAYLOAD_TYPE_PATH, "PAYLOAD_TYPE_PATH" },
  { PAYLOAD_TYPE_TRACE, "PAYLOAD_TYPE_TRACE" },
  { PAYLOAD_TYPE_MULTIPART, "PAYLOAD_TYPE_MULTIPART" },
  { PAYLOAD_TYPE_CONTROL, "PAYLOAD_TYPE_CONTROL" },
  { PAYLOAD_TYPE_OTA, "PAYLOAD_TYPE_OTA" },
  { PAYLOAD_TYPE_RAW_CUSTOM, "PAYLOAD_TYPE_RAW_CUSTOM" },
};

} // namespace

const char* payloadTypeName(uint8_t type) {
  for (unsigned i = 0; i < sizeof(PAYLOAD_TYPE_NAMES) / sizeof(PAYLOAD_TYPE_NAMES[0]); i++) {
    if (PAYLOAD_TYPE_NAMES[i].type == type) return PAYLOAD_TYPE_NAMES[i].name;
  }
  return nullptr;
}

void formatPayloadType(uint8_t type, char* buf, size_t cap) {
  const char* name = payloadTypeName(type);
  if (name) {
    snprintf(buf, cap, "%s (0x%02X)", name, type);
  } else {
    snprintf(buf, cap, "UNKNOWN (0x%02X)", type);
  }
}

Packet::Packet() {
  header = 0;
  path_len = 0;
  payload_len = 0;
}

bool Packet::isValidPathLen(uint8_t path_len) {
  uint8_t hash_count = path_len & 63;
  uint8_t hash_size = (path_len >> 6) + 1;
  if (hash_size == 4) return false;  // Reserved for future
  return hash_count*hash_size <= MAX_PATH_SIZE;
}

size_t Packet::writePath(uint8_t* dest, const uint8_t* src, uint8_t path_len) {
  uint8_t hash_count = path_len & 63;
  uint8_t hash_size = (path_len >> 6) + 1;
  size_t len = hash_count*hash_size;
  if (len > MAX_PATH_SIZE) {
    MESH_DEBUG_PRINTLN("Packet::copyPath, invalid path_len=%d", (uint32_t)path_len);
    return 0;   // Error
  }
  memcpy(dest, src, len);
  return len;
}

uint8_t Packet::copyPath(uint8_t* dest, const uint8_t* src, uint8_t path_len) {
  writePath(dest, src, path_len);
  return path_len;
}

int Packet::getRawLength() const {
  return 2 + getPathByteLen() + payload_len + (hasTransportCodes() ? 4 : 0);
}

void Packet::calculatePacketHash(uint8_t* hash) const {
  SHA256 sha;
  uint8_t t = getPayloadType();
  sha.update(&t, 1);
  if (t == PAYLOAD_TYPE_TRACE) {
    sha.update(&path_len, sizeof(path_len));   // CAVEAT: TRACE packets can revisit same node on return path
  }
  sha.update(payload, payload_len);
  sha.finalize(hash, MAX_HASH_SIZE);
}

uint8_t Packet::writeTo(uint8_t dest[]) const {
  uint8_t i = 0;
  dest[i++] = header;
  if (hasTransportCodes()) {
    memcpy(&dest[i], &transport_codes[0], 2); i += 2;
    memcpy(&dest[i], &transport_codes[1], 2); i += 2;
  }
  dest[i++] = path_len;
  i += writePath(&dest[i], path, path_len);
  memcpy(&dest[i], payload, payload_len); i += payload_len;
  return i;
}

bool Packet::readFrom(const uint8_t src[], uint8_t len) {
  uint8_t i = 0;
  header = src[i++];
  if (hasTransportCodes()) {
    memcpy(&transport_codes[0], &src[i], 2); i += 2;
    memcpy(&transport_codes[1], &src[i], 2); i += 2;
  } else {
    transport_codes[0] = transport_codes[1] = 0;
  }
  path_len = src[i++];
  if (!isValidPathLen(path_len)) return false;   // bad encoding

  uint8_t bl = getPathByteLen();
  memcpy(path, &src[i], bl); i += bl;

  if (i >= len) return false;   // bad encoding
  payload_len = len - i;
  if (payload_len > sizeof(payload)) return false;  // bad encoding
  memcpy(payload, &src[i], payload_len); //i += payload_len;
  return true;   // success
}

}