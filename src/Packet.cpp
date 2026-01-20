#include "Packet.h"
#include <string.h>
#include <SHA256.h>

namespace mesh {

Packet::Packet() {
  header = 0;
  path_len = 0;
  payload_len = 0;
}

int Packet::getRawLength() const {
  return 2 + path_len + payload_len + (hasTransportCodes() ? 4 : 0);
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
  memcpy(&dest[i], path, path_len); i += path_len;
  memcpy(&dest[i], payload, payload_len); i += payload_len;
  return i;
}

bool Packet::readFrom(const uint8_t src[], uint16_t len) {
  if (len == 0) return false;  // minimum packet needs at least header
  uint16_t i = 0;
  header = src[i++];
  if (hasTransportCodes()) {
    if (i + 4 > len) return false;  // need 4 bytes for transport codes
    memcpy(&transport_codes[0], &src[i], 2); i += 2;
    memcpy(&transport_codes[1], &src[i], 2); i += 2;
  } else {
    transport_codes[0] = transport_codes[1] = 0;
  }
  if (i >= len) return false;  // need at least path_len byte
  path_len = src[i++];
  if (path_len > sizeof(path)) return false;   // bad encoding
  if (i + path_len > len) return false;  // path extends beyond buffer
  memcpy(path, &src[i], path_len); i += path_len;
  if (i >= len) return false;   // bad encoding
  payload_len = len - i;
  if (payload_len > sizeof(payload)) return false;  // bad encoding
  memcpy(payload, &src[i], payload_len); //i += payload_len;
  return true;   // success
}

}