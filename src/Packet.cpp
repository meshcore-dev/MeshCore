#include "Packet.h"
#include <string.h>
#include <SHA256.h>

namespace mesh {

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

bool Packet::readFrom(const uint8_t src[], size_t len) {
  if (src == NULL || len < 2) return false;  // needs at least header + path_len

  size_t i = 0;
  uint8_t new_header = src[i++];
  uint16_t new_transport_codes[2] = {0, 0};

  uint8_t route_type = new_header & PH_ROUTE_MASK;
  bool has_transport = route_type == ROUTE_TYPE_TRANSPORT_FLOOD || route_type == ROUTE_TYPE_TRANSPORT_DIRECT;
  if (has_transport) {
    if (len - i < 4) return false;   // truncated transport header
    memcpy(&new_transport_codes[0], &src[i], 2); i += 2;
    memcpy(&new_transport_codes[1], &src[i], 2); i += 2;
  }

  if (len - i < 1) return false;   // missing path_len
  uint8_t new_path_len = src[i++];
  uint8_t payload_ver = (new_header >> PH_VER_SHIFT) & PH_VER_MASK;
  if (payload_ver > PAYLOAD_VER_1 || !isValidPathLen(new_path_len)) return false;

  size_t path_byte_len = (new_path_len & 63) * ((new_path_len >> 6) + 1);
  if (len - i < path_byte_len) return false;   // truncated path

  uint8_t new_path[MAX_PATH_SIZE];
  memcpy(new_path, &src[i], path_byte_len); i += path_byte_len;

  if (i >= len) return false;   // empty payload is invalid encoding
  size_t new_payload_len = len - i;
  if (new_payload_len > sizeof(payload)) return false;

  // Only commit state after all bounds checks succeed.
  header = new_header;
  transport_codes[0] = new_transport_codes[0];
  transport_codes[1] = new_transport_codes[1];
  path_len = new_path_len;
  payload_len = new_payload_len;
  memcpy(path, new_path, path_byte_len);
  memcpy(payload, &src[i], new_payload_len);
  return true;
}

}
