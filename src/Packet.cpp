#include "Packet.h"
#include <string.h>
#include <SHA256.h>

namespace mesh {

namespace {

bool hasCompleteCiphertext(size_t payload_len, size_t prefix_len) {
  const size_t overhead = prefix_len + CIPHER_MAC_SIZE;
  return payload_len >= overhead + CIPHER_BLOCK_SIZE &&
         (payload_len - overhead) % CIPHER_BLOCK_SIZE == 0;
}

bool isValidPayload(uint8_t header, uint8_t path_len, const uint8_t* payload, size_t payload_len) {
  const uint8_t type = (header >> PH_TYPE_SHIFT) & PH_TYPE_MASK;
  switch (type) {
    case PAYLOAD_TYPE_REQ:
    case PAYLOAD_TYPE_RESPONSE:
    case PAYLOAD_TYPE_TXT_MSG:
    case PAYLOAD_TYPE_PATH:
      return hasCompleteCiphertext(payload_len, 2);
    case PAYLOAD_TYPE_ACK:
      return payload_len >= MIN_ACK_PAYLOAD_SIZE && payload_len <= MAX_ACK_PAYLOAD_SIZE;
    case PAYLOAD_TYPE_ADVERT:
      return payload_len >= PUB_KEY_SIZE + sizeof(uint32_t) + SIGNATURE_SIZE &&
             payload_len <= PUB_KEY_SIZE + sizeof(uint32_t) + SIGNATURE_SIZE + MAX_ADVERT_DATA_SIZE;
    case PAYLOAD_TYPE_GRP_TXT:
    case PAYLOAD_TYPE_GRP_DATA:
      return hasCompleteCiphertext(payload_len, 1);
    case PAYLOAD_TYPE_ANON_REQ:
      return hasCompleteCiphertext(payload_len, 1 + PUB_KEY_SIZE);
    case PAYLOAD_TYPE_TRACE: {
      if ((path_len & 0xc0) != 0 || payload_len < 9) return false;
      const uint8_t flags = payload[8];
      const size_t hash_size = 1u << (flags & 0x03);
      return (flags & 0xfc) == 0 && (payload_len - 9) % hash_size == 0;
    }
    case PAYLOAD_TYPE_MULTIPART:
      if (payload_len == 0) return false;
      if ((payload[0] & 0x0f) == PAYLOAD_TYPE_ACK) {
        const size_t ack_len = payload_len - 1;
        return ack_len >= MIN_ACK_PAYLOAD_SIZE && ack_len <= MAX_ACK_PAYLOAD_SIZE;
      }
      return true;
    case PAYLOAD_TYPE_CONTROL:
      if (payload_len == 0) return false;
      if (type == PAYLOAD_TYPE_CONTROL && (payload[0] & 0x80) != 0) {
        const uint8_t route = header & PH_ROUTE_MASK;
        return (route == ROUTE_TYPE_DIRECT || route == ROUTE_TYPE_TRANSPORT_DIRECT) &&
               (path_len & 0x3f) == 0;
      }
      return true;
    default:
      return true;
  }
}

}  // namespace

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

bool Packet::hasCompletePath(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || !isValidPathLen(data[0])) return false;
  const size_t path_byte_len = (data[0] & 63) * ((data[0] >> 6) + 1);
  return path_byte_len <= len - 1;
}

bool Packet::isValidPathPlaintext(const uint8_t* data, size_t len) {
  if (len < 2 || !hasCompletePath(data, len)) return false;
  const size_t path_byte_len = (data[0] & 63) * ((data[0] >> 6) + 1);
  return path_byte_len < len - 1;  // One byte after the path is required for extra_type.
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
  if (src == nullptr || len == 0 || len > MAX_TRANS_UNIT) return false;

  size_t i = 0;
  const uint8_t decoded_header = src[i++];
  if (((decoded_header >> PH_VER_SHIFT) & PH_VER_MASK) > PAYLOAD_VER_1) return false;

  uint16_t decoded_transport_codes[2] = {};
  const bool has_transport_codes =
      (decoded_header & PH_ROUTE_MASK) == ROUTE_TYPE_TRANSPORT_FLOOD ||
      (decoded_header & PH_ROUTE_MASK) == ROUTE_TYPE_TRANSPORT_DIRECT;
  if (has_transport_codes) {
    if (len - i < sizeof(decoded_transport_codes)) return false;
    memcpy(decoded_transport_codes, &src[i], sizeof(decoded_transport_codes));
    i += sizeof(decoded_transport_codes);
  }

  if (i >= len) return false;
  const uint8_t decoded_path_len = src[i++];
  if (!isValidPathLen(decoded_path_len)) return false;

  const uint8_t hash_count = decoded_path_len & 63;
  const uint8_t hash_size = (decoded_path_len >> 6) + 1;
  const size_t path_byte_len = hash_count * hash_size;
  if (len - i < path_byte_len) return false;

  const size_t decoded_payload_len = len - i - path_byte_len;
  if (decoded_payload_len > sizeof(payload)) return false;

  const uint8_t* decoded_payload = &src[i + path_byte_len];
  if (!isValidPayload(decoded_header, decoded_path_len, decoded_payload, decoded_payload_len)) {
    return false;
  }

  header = decoded_header;
  transport_codes[0] = decoded_transport_codes[0];
  transport_codes[1] = decoded_transport_codes[1];
  path_len = decoded_path_len;
  memcpy(path, &src[i], path_byte_len);
  i += path_byte_len;
  payload_len = decoded_payload_len;
  memcpy(payload, decoded_payload, payload_len);
  return true;   // success
}

}
