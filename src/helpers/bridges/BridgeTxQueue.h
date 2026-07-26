#pragma once

#include "BridgeFrameQueue.h"

#include <atomic>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Sink that hands a framed blob to the radio.
 *
 * Implemented by the bridge; a fake stands in for it under test.
 */
class BridgeFrameSender {
public:
  virtual ~BridgeFrameSender() = default;

  /**
   * @return true if the radio accepted the frame and a completion will follow;
   *         false if it refused it outright (e.g. ESP_ERR_ESPNOW_NO_MEM), in
   *         which case no completion is coming and the frame must be retried.
   */
  virtual bool sendFrame(const uint8_t *frame, size_t len) = 0;
};

/**
 * @brief Paces bridge transmissions: one frame in flight, retried on failure.
 *
 * ESP-NOW refuses a send with ESP_ERR_ESPNOW_NO_MEM when its internal queue is
 * full, which is what happens when packets arrive in a burst. The frame stays
 * queued and is retried, so a burst costs latency instead of messages.
 *
 * Every path out of the in-flight state is bounded: if the completion callback
 * never fires the frame times out, so a lost callback cannot wedge the queue.
 */
class BridgeTxQueue {
public:
  /**
   * @param capacity        frames that can be queued at once
   * @param max_frame_len   largest frame accepted
   * @param max_attempts    transmissions tried per frame before giving up
   * @param ack_timeout_ms  how long to wait for a completion before retrying
   * @param retry_delay_ms  pause between attempts
   */
  BridgeTxQueue(uint8_t capacity, uint16_t max_frame_len, uint8_t max_attempts,
                uint32_t ack_timeout_ms, uint32_t retry_delay_ms);
  ~BridgeTxQueue();

  BridgeTxQueue(const BridgeTxQueue &) = delete;
  BridgeTxQueue &operator=(const BridgeTxQueue &) = delete;

  void setSender(BridgeFrameSender *sender) { _sender = sender; }

  /** Queue a frame for transmission. False if it was dropped (full/oversize). */
  bool enqueue(const uint8_t *frame, size_t len);

  /** Drive the state machine. Call from the main loop with the current millis(). */
  void loop(uint32_t now);

  /**
   * @brief Report the radio's completion. Safe to call from a driver task.
   *
   * Only stores a flag; the work happens in loop() on the main task.
   */
  void onSendComplete(bool ok);

  /** Drop everything queued and in flight, e.g. when the bridge is stopped. */
  void reset();

  bool isIdle() const { return _state == STATE_IDLE; }

  /** Frames the radio confirmed. */
  uint32_t getSent() const { return _sent; }
  /** Transmissions beyond the first attempt for a frame. */
  uint32_t getRetries() const { return _retries; }
  /** Frames abandoned after max_attempts. */
  uint32_t getFailed() const { return _failed; }
  /** Completions that never arrived within ack_timeout_ms. */
  uint32_t getTimeouts() const { return _timeouts; }
  /** Times the radio refused a frame outright. */
  uint32_t getRadioRefusals() const { return _radio_refusals; }

  BridgeFrameQueue &queue() { return _queue; }
  const BridgeFrameQueue &queue() const { return _queue; }

  void resetStats();

private:
  enum State : uint8_t {
    STATE_IDLE,       ///< nothing loaded
    STATE_PENDING,    ///< frame loaded, waiting until it is time to (re)transmit
    STATE_IN_FLIGHT,  ///< handed to the radio, waiting for a completion
  };

  void transmit(uint32_t now);
  void scheduleRetry(uint32_t now);

  BridgeFrameQueue _queue;
  BridgeFrameSender *_sender;

  uint8_t _max_attempts;
  uint32_t _ack_timeout_ms;
  uint32_t _retry_delay_ms;

  uint8_t *_pending;
  uint16_t _pending_len;
  uint8_t _attempts;

  State _state;
  uint32_t _sent_at;
  uint32_t _next_attempt_at;

  // The radio owes exactly one completion per accepted send, but its callback
  // carries no token identifying which send it belongs to. Counting completions
  // rather than latching a single flag lets loop() tell a completion for the
  // current attempt apart from one owed by an attempt already timed out or reset
  // away, which would otherwise confirm a frame nothing acknowledged.
  std::atomic<uint32_t> _cb_count;   ///< bumped by the driver task per completion
  std::atomic<uint8_t> _cb_status;   ///< outcome of the most recent completion
  uint32_t _cb_consumed;             ///< completions loop() has already accounted for
  uint32_t _owed;                    ///< completions still due from abandoned attempts

  uint32_t _sent;
  uint32_t _retries;
  uint32_t _failed;
  uint32_t _timeouts;
  uint32_t _radio_refusals;
};
