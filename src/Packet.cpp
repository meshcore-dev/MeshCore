#include "Packet.h"
#include "Utils.h"
#include <string.h>
#include <SHA256.h>

namespace mesh {

static const uint8_t ZERO_HASH[MAX_HASH_SIZE] = { 0 };

Packet::Packet() {
  header = 0;
  path_len = 0;
  payload_len = 0;
  memcpy(hash, ZERO_HASH, MAX_HASH_SIZE);
  memset(hash_hex, 0, sizeof(hash_hex));
  sending_attempts = 0;
  final_hop_ack_resend = false;
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

uint8_t *Packet::calculatePacketHash() const {
  if (memcmp(this->hash, ZERO_HASH, MAX_HASH_SIZE) == 0) {
    SHA256 sha;
    uint8_t t = getPayloadType();
    sha.update(&t, 1);
    if (t == PAYLOAD_TYPE_TRACE) {
      sha.update(&path_len, sizeof(path_len)); // CAVEAT: TRACE packets can revisit same node on return path
    }
    sha.update(payload, payload_len);
    sha.finalize((uint8_t *)this->hash, MAX_HASH_SIZE);
  }
  return (uint8_t *)this->hash;
}

const char* Packet::getHashHex() const {
  calculatePacketHash();
  Utils::toHex(hash_hex, hash, MAX_HASH_SIZE);
  return hash_hex;
}

bool Packet::isRetryMatch(const Packet* outbound) const {
  // Only packets of the same payload type can be retries of each other.
  if (this->getPayloadType() != outbound->getPayloadType()) return false;

  // TRACE packets append an SNR byte to path[] and increment path_len per hop.
  // A forwarded TRACE therefore has the same payload and a longer path with the
  // previous hop's path as a prefix. Accept any downstream hop, not just the
  // immediate next hop, so retries are canceled as soon as we overhear a later
  // forward of the same TRACE.
  if (this->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    if (this->payload_len != outbound->payload_len) return false;
    if (memcmp(this->payload, outbound->payload, this->payload_len) != 0) return false;
    if (this->path_len <= outbound->path_len) return false;
    if (outbound->path_len > 0 &&
        memcmp(this->path, outbound->path, outbound->path_len) != 0) {
      return false;
    }
    return true;
  }

  // Default behaviour for all other payload types: compare cached hashes.
  const uint8_t* h1 = this->calculatePacketHash();
  const uint8_t* h2 = outbound->calculatePacketHash();
  return memcmp(h1, h2, MAX_HASH_SIZE) == 0;
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