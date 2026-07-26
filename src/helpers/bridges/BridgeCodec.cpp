#include "BridgeCodec.h"

#include <string.h>

namespace {

/**
 * XOR @p data with @p secret, as if @p data started @p offset bytes into the
 * encrypted region. Lets decode() decrypt the checksum and the payload
 * separately without staging the whole frame in a temporary buffer.
 */
void xorCryptAt(uint8_t *data, size_t len, const char *secret, size_t offset) {
  if (secret == nullptr) return;

  const size_t key_len = strlen(secret);
  if (key_len == 0) return;  // blank secret means "no encryption", never a divide by zero

  for (size_t i = 0; i < len; i++) {
    data[i] ^= (uint8_t)secret[(offset + i) % key_len];
  }
}

}  // namespace

uint16_t BridgeCodec::fletcher16(const uint8_t *data, size_t len) {
  uint8_t sum1 = 0, sum2 = 0;

  for (size_t i = 0; i < len; i++) {
    sum1 = (sum1 + data[i]) % 255;
    sum2 = (sum2 + sum1) % 255;
  }

  return (uint16_t)((sum2 << 8) | sum1);
}

void BridgeCodec::xorCrypt(uint8_t *data, size_t len, const char *secret) {
  xorCryptAt(data, len, secret, 0);
}

int BridgeCodec::encode(const uint8_t *payload, size_t payload_len, const char *secret, uint8_t *out,
                        size_t out_cap) {
  if (payload == nullptr || payload_len == 0) return -1;

  const size_t frame_len = FRAME_OVERHEAD + payload_len;
  if (frame_len > out_cap) return -1;  // refuse rather than overrun the caller's buffer

  out[0] = (uint8_t)((BRIDGE_PACKET_MAGIC >> 8) & 0xFF);
  out[1] = (uint8_t)(BRIDGE_PACKET_MAGIC & 0xFF);

  const uint16_t checksum = fletcher16(payload, payload_len);
  out[2] = (uint8_t)((checksum >> 8) & 0xFF);
  out[3] = (uint8_t)(checksum & 0xFF);

  memcpy(out + FRAME_OVERHEAD, payload, payload_len);
  xorCryptAt(out + BRIDGE_MAGIC_SIZE, BRIDGE_CHECKSUM_SIZE + payload_len, secret, 0);

  return (int)frame_len;
}

int BridgeCodec::decode(const uint8_t *frame, size_t frame_len, const char *secret, uint8_t *out,
                        size_t out_cap) {
  if (frame == nullptr || frame_len <= FRAME_OVERHEAD) return -1;  // truncated, or carries no payload

  const uint16_t magic = (uint16_t)((frame[0] << 8) | frame[1]);
  if (magic != BRIDGE_PACKET_MAGIC) return -1;

  const size_t payload_len = frame_len - FRAME_OVERHEAD;
  if (payload_len > out_cap) return -1;

  uint8_t checksum_bytes[BRIDGE_CHECKSUM_SIZE] = { frame[2], frame[3] };
  xorCryptAt(checksum_bytes, BRIDGE_CHECKSUM_SIZE, secret, 0);
  const uint16_t received_checksum = (uint16_t)((checksum_bytes[0] << 8) | checksum_bytes[1]);

  memcpy(out, frame + FRAME_OVERHEAD, payload_len);
  xorCryptAt(out, payload_len, secret, BRIDGE_CHECKSUM_SIZE);

  if (fletcher16(out, payload_len) != received_checksum) return -1;

  return (int)payload_len;
}
