#include "BridgeSerialFramer.h"

#include <string.h>

namespace {

constexpr uint8_t MAGIC_HI = (BridgeCodec::BRIDGE_PACKET_MAGIC >> 8) & 0xFF;
constexpr uint8_t MAGIC_LO = BridgeCodec::BRIDGE_PACKET_MAGIC & 0xFF;

}  // namespace

BridgeSerialFramer::BridgeSerialFramer(uint16_t max_payload_len)
    : _max_payload_len(max_payload_len), _payload(nullptr), _state(STATE_MAGIC_HI),
      _expected_len(0), _payload_pos(0), _checksum(0), _frames_decoded(0), _checksum_errors(0),
      _length_errors(0), _resync_bytes(0) {
  _payload = new uint8_t[max_payload_len];
}

BridgeSerialFramer::~BridgeSerialFramer() {
  delete[] _payload;
}

uint16_t BridgeSerialFramer::offer(uint8_t b) {
  switch (_state) {
    case STATE_MAGIC_HI:
      if (b == MAGIC_HI) {
        _state = STATE_MAGIC_LO;
      } else {
        _resync_bytes++;
      }
      return 0;

    case STATE_MAGIC_LO:
      if (b == MAGIC_LO) {
        _state = STATE_LEN_HI;
      } else if (b == MAGIC_HI) {
        // The previous byte was noise, but this one could still start the header.
        _resync_bytes++;
      } else {
        _resync_bytes += 2;  // the earlier high byte and this one were both noise
        _state = STATE_MAGIC_HI;
      }
      return 0;

    case STATE_LEN_HI:
      _expected_len = (uint16_t)(b << 8);
      _state = STATE_LEN_LO;
      return 0;

    case STATE_LEN_LO:
      _expected_len |= b;
      if (_expected_len == 0 || _expected_len > _max_payload_len) {
        // Never a valid mesh packet, and would run off the buffer.
        _length_errors++;
        _state = STATE_MAGIC_HI;
        return 0;
      }
      _payload_pos = 0;
      _state = STATE_PAYLOAD;
      return 0;

    case STATE_PAYLOAD:
      _payload[_payload_pos++] = b;
      if (_payload_pos == _expected_len) {
        _state = STATE_CHECKSUM_HI;
      }
      return 0;

    case STATE_CHECKSUM_HI:
      _checksum = (uint16_t)(b << 8);
      _state = STATE_CHECKSUM_LO;
      return 0;

    case STATE_CHECKSUM_LO:
      _checksum |= b;
      _state = STATE_MAGIC_HI;
      if (BridgeCodec::fletcher16(_payload, _expected_len) == _checksum) {
        _frames_decoded++;
        return _expected_len;
      }
      _checksum_errors++;
      return 0;
  }

  return 0;
}

void BridgeSerialFramer::reset() {
  _state = STATE_MAGIC_HI;
  _expected_len = 0;
  _payload_pos = 0;
}

void BridgeSerialFramer::resetStats() {
  _frames_decoded = 0;
  _checksum_errors = 0;
  _length_errors = 0;
  _resync_bytes = 0;
}

int BridgeSerialFramer::encode(const uint8_t *payload, size_t payload_len, uint8_t *out,
                               size_t out_cap) {
  if (payload == nullptr || payload_len == 0) return -1;

  const size_t frame_len = FRAME_OVERHEAD + payload_len;
  if (frame_len > out_cap) return -1;  // refuse rather than overrun the caller's buffer

  out[0] = MAGIC_HI;
  out[1] = MAGIC_LO;
  out[2] = (uint8_t)((payload_len >> 8) & 0xFF);
  out[3] = (uint8_t)(payload_len & 0xFF);

  memcpy(out + BridgeCodec::BRIDGE_MAGIC_SIZE + BridgeCodec::BRIDGE_LENGTH_SIZE, payload,
         payload_len);

  const uint16_t checksum = BridgeCodec::fletcher16(payload, payload_len);
  out[4 + payload_len] = (uint8_t)((checksum >> 8) & 0xFF);
  out[5 + payload_len] = (uint8_t)(checksum & 0xFF);

  return (int)frame_len;
}
