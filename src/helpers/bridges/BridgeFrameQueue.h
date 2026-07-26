#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Fixed-capacity FIFO of raw bridge frames.
 *
 * Lets a bridge accept a frame in a driver-task context (the ESP-NOW receive
 * callback runs on the Wi-Fi task) without touching the mesh packet pool, the
 * inbound queue or the seen-packet table, none of which are synchronised.
 *
 * Safe for one producer and one consumer running concurrently: the producer only
 * advances _head, the consumer only advances _tail, and each index is published
 * with a release store and read with an acquire load. NOT safe for multiple
 * producers or consumers.
 *
 * Frames dropped for want of room are counted, so loss is visible over the CLI.
 */
class BridgeFrameQueue {
public:
  /**
   * @param capacity       maximum number of frames held at once
   * @param max_frame_len  maximum size of a single frame, in bytes
   */
  BridgeFrameQueue(uint8_t capacity, uint16_t max_frame_len);
  ~BridgeFrameQueue();

  BridgeFrameQueue(const BridgeFrameQueue &) = delete;
  BridgeFrameQueue &operator=(const BridgeFrameQueue &) = delete;

  /**
   * @brief Append a frame. Producer side.
   *
   * @return true if stored; false if the frame was empty, longer than
   *         max_frame_len, or the queue was full. Every false bumps a counter.
   */
  bool push(const uint8_t *frame, size_t len);

  /**
   * @brief Remove the oldest frame. Consumer side.
   *
   * If the frame does not fit @p out_cap it is discarded and counted, rather
   * than left at the head where it would wedge the queue permanently.
   *
   * @return bytes written to @p out, or 0 if the queue was empty.
   */
  size_t pop(uint8_t *out, size_t out_cap);

  bool isEmpty() const;
  uint8_t count() const;
  uint8_t capacity() const { return _capacity; }
  uint16_t maxFrameLen() const { return _max_frame_len; }

  uint32_t getPushed() const { return _pushed.load(std::memory_order_relaxed); }
  uint32_t getPopped() const { return _popped; }
  /** Frames lost because the queue was full — the consumer is not keeping up. */
  uint32_t getDroppedFull() const { return _dropped_full.load(std::memory_order_relaxed); }
  /**
   * Frames rejected on the way in because they were empty or longer than
   * max_frame_len, plus frames discarded on the way out because they did not fit
   * the consumer's buffer.
   */
  uint32_t getDroppedOversize() const {
    return _dropped_oversize_in.load(std::memory_order_relaxed) + _dropped_oversize_out;
  }
  /** Deepest the queue has ever been; shows how close to full it runs. */
  uint8_t getHighWaterMark() const { return _high_water.load(std::memory_order_relaxed); }

  void resetStats();

private:
  uint8_t _capacity;
  uint16_t _max_frame_len;
  uint8_t *_slots;      ///< _capacity contiguous blocks of _max_frame_len bytes
  uint16_t *_lengths;   ///< payload length held in each slot

  std::atomic<uint32_t> _head;  ///< producer-owned, total frames ever stored
  std::atomic<uint32_t> _tail;  ///< consumer-owned, total frames ever removed

  // Producer-side counters. resetStats() and the getters run on the consumer, so
  // these have two writers and are atomic: a plain read-modify-write here could
  // lose a reset issued from the CLI, or report a torn value.
  std::atomic<uint32_t> _pushed;
  std::atomic<uint32_t> _dropped_full;
  std::atomic<uint32_t> _dropped_oversize_in;
  std::atomic<uint8_t> _high_water;

  // Consumer-side counters. pop(), resetStats() and the getters all run on the
  // consumer, so these have a single writer and need no synchronisation.
  uint32_t _popped;
  uint32_t _dropped_oversize_out;
};
