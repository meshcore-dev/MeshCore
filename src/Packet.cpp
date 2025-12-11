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

bool Packet::readFrom(const uint8_t src[], uint8_t len) {
  uint8_t i = 0;
  
  // Validate minimum packet size
  if (len < 2) return false;  // need at least header + path_len
  
  header = src[i++];
  
  // Validate transport codes bounds before reading
  if (hasTransportCodes()) {
    if (i + 4 > len) return false;  // need 4 bytes for transport codes
    memcpy(&transport_codes[0], &src[i], 2); i += 2;
    memcpy(&transport_codes[1], &src[i], 2); i += 2;
  } else {
    transport_codes[0] = transport_codes[1] = 0;
  }
  
  // Validate path_len bounds before reading
  if (i >= len) return false;  // need at least 1 byte for path_len
  path_len = src[i++];
  
  // Validate path_len value and buffer bounds
  if (path_len > sizeof(path)) return false;   // path too large
  if (i + path_len > len) return false;  // not enough data for path
  memcpy(path, &src[i], path_len); i += path_len;
  
  // Validate payload bounds
  if (i > len) return false;   // overflow check
  payload_len = len - i;
  if (payload_len > sizeof(payload)) return false;  // payload too large
  
  memcpy(payload, &src[i], payload_len);
  return true;   // success
}

}