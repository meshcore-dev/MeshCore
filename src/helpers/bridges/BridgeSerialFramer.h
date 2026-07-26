#pragma once

#include "BridgeCodec.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Byte-at-a-time decoder for the length-prefixed serial bridge framing.
 *
 * Wire layout:
 *   [2 bytes] magic header (BridgeCodec::BRIDGE_PACKET_MAGIC, big endian)
 *   [2 bytes] payload length, big endian
 *   [n bytes] payload (a mesh packet blob)
 *   [2 bytes] Fletcher-16 of the payload, big endian
 *
 * A serial line has no packet boundaries, so the decoder resynchronises from the
 * byte after any rejection and counts why, which makes a link that is quietly
 * corrupting frames visible. No Arduino dependencies, so it unit tests natively.
 */
class BridgeSerialFramer {
public:
  /** Bytes of framing around the payload: magic + length + checksum. */
  static constexpr uint16_t FRAME_OVERHEAD =
      BridgeCodec::BRIDGE_MAGIC_SIZE + BridgeCodec::BRIDGE_LENGTH_SIZE +
      BridgeCodec::BRIDGE_CHECKSUM_SIZE;

  /**
   * @brief Wrap a payload in the framing this class decodes.
   *
   * @return framed length, or -1 if it would not fit @p out_cap (in which case
   *         @p out is left untouched)
   */
  static int encode(const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_cap);

  /** @param max_payload_len largest payload accepted; longer length fields are rejected */
  explicit BridgeSerialFramer(uint16_t max_payload_len);
  ~BridgeSerialFramer();

  BridgeSerialFramer(const BridgeSerialFramer &) = delete;
  BridgeSerialFramer &operator=(const BridgeSerialFramer &) = delete;

  /**
   * @brief Feed one byte from the link.
   *
   * @return the payload length now available from payload(), or 0 if the frame
   *         is still incomplete or was rejected. The returned payload stays
   *         valid until the next offer() call.
   */
  uint16_t offer(uint8_t b);

  /** Payload of the frame most recently completed by offer(). */
  const uint8_t *payload() const { return _payload; }

  /** Abandon any partly received frame and hunt for a fresh magic header. */
  void reset();

  /** Complete frames that passed their checksum. */
  uint32_t getFramesDecoded() const { return _frames_decoded; }
  /** Frames discarded because the payload did not match its checksum. */
  uint32_t getChecksumErrors() const { return _checksum_errors; }
  /** Frames discarded because the length field was zero or too large. */
  uint32_t getLengthErrors() const { return _length_errors; }
  /** Bytes thrown away while hunting for a magic header, i.e. line noise. */
  uint32_t getResyncBytes() const { return _resync_bytes; }

  void resetStats();

private:
  enum State : uint8_t {
    STATE_MAGIC_HI,
    STATE_MAGIC_LO,
    STATE_LEN_HI,
    STATE_LEN_LO,
    STATE_PAYLOAD,
    STATE_CHECKSUM_HI,
    STATE_CHECKSUM_LO,
  };

  uint16_t _max_payload_len;
  uint8_t *_payload;

  State _state;
  uint16_t _expected_len;
  uint16_t _payload_pos;
  uint16_t _checksum;

  uint32_t _frames_decoded;
  uint32_t _checksum_errors;
  uint32_t _length_errors;
  uint32_t _resync_bytes;
};
