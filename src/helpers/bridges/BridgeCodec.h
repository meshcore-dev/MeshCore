#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Wire framing shared by every bridge implementation.
 *
 * Kept free of Arduino/ESP-IDF dependencies so it can be unit tested natively.
 *
 * Frame layout produced by encode():
 *   [2 bytes] magic header (BRIDGE_PACKET_MAGIC, big endian, sent in clear)
 *   [2 bytes] Fletcher-16 of the payload (big endian, encrypted)
 *   [n bytes] payload (encrypted)
 *
 * The checksum is calculated over the plaintext payload and then encrypted along
 * with it, so a frame from a network using a different secret decrypts to garbage
 * and fails validation. That is what provides network isolation.
 */
class BridgeCodec {
public:
  /** Magic number identifying a bridge frame and providing frame sync. */
  static constexpr uint16_t BRIDGE_PACKET_MAGIC = 0xC03E;

  static constexpr uint16_t BRIDGE_MAGIC_SIZE = sizeof(uint16_t);
  static constexpr uint16_t BRIDGE_LENGTH_SIZE = sizeof(uint16_t);
  static constexpr uint16_t BRIDGE_CHECKSUM_SIZE = sizeof(uint16_t);

  /** Bytes of framing overhead that encode() adds around the payload. */
  static constexpr size_t FRAME_OVERHEAD = BRIDGE_MAGIC_SIZE + BRIDGE_CHECKSUM_SIZE;

  /**
   * @brief Fletcher-16 checksum.
   *
   * Based on https://en.wikipedia.org/wiki/Fletcher%27s_checksum
   */
  static uint16_t fletcher16(const uint8_t *data, size_t len);

  /**
   * @brief XOR a buffer with a repeating secret, in place.
   *
   * Does nothing when @p secret is null or empty, so a node configured with a
   * blank secret still interoperates (unencrypted) instead of dividing by zero.
   */
  static void xorCrypt(uint8_t *data, size_t len, const char *secret);

  /**
   * @brief Frame and encrypt a mesh packet blob.
   *
   * @param payload      the mesh packet as produced by Packet::writeTo()
   * @param payload_len  length of @p payload
   * @param secret       shared network secret (may be null/empty)
   * @param out          destination buffer
   * @param out_cap      capacity of @p out
   * @return encoded frame length, or -1 if the frame would not fit in @p out_cap
   *         (in which case @p out is left untouched)
   */
  static int encode(const uint8_t *payload, size_t payload_len, const char *secret, uint8_t *out,
                    size_t out_cap);

  /**
   * @brief Validate, decrypt and unwrap a frame produced by encode().
   *
   * @param frame      the received frame, starting at the magic header
   * @param frame_len  length of @p frame
   * @param secret     shared network secret (may be null/empty)
   * @param out        destination for the decrypted payload
   * @param out_cap    capacity of @p out
   * @return payload length written to @p out, or -1 if the frame is truncated,
   *         has the wrong magic, fails its checksum, or will not fit @p out_cap
   */
  static int decode(const uint8_t *frame, size_t frame_len, const char *secret, uint8_t *out,
                    size_t out_cap);
};
