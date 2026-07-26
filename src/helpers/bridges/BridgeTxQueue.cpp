#include "BridgeTxQueue.h"

namespace {

/** Rollover-safe "has `deadline` been reached by `now`?". */
inline bool hasElapsed(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

}  // namespace

BridgeTxQueue::BridgeTxQueue(uint8_t capacity, uint16_t max_frame_len, uint8_t max_attempts,
                             uint32_t ack_timeout_ms, uint32_t retry_delay_ms)
    : _queue(capacity, max_frame_len), _sender(nullptr),
      _max_attempts(max_attempts < 1 ? 1 : max_attempts), _ack_timeout_ms(ack_timeout_ms),
      _retry_delay_ms(retry_delay_ms), _pending(nullptr), _pending_len(0), _attempts(0),
      _state(STATE_IDLE), _sent_at(0), _next_attempt_at(0), _cb_count(0), _cb_status(0),
      _cb_consumed(0), _owed(0), _sent(0), _retries(0), _failed(0), _timeouts(0),
      _radio_refusals(0) {
  _pending = new uint8_t[max_frame_len];
}

BridgeTxQueue::~BridgeTxQueue() {
  delete[] _pending;
}

bool BridgeTxQueue::enqueue(const uint8_t *frame, size_t len) {
  return _queue.push(frame, len);
}

void BridgeTxQueue::onSendComplete(bool ok) {
  // Runs on the radio driver's task; loop() acts on it from the main task.
  _cb_status.store(ok ? 1 : 0, std::memory_order_relaxed);
  _cb_count.fetch_add(1, std::memory_order_release);
}

void BridgeTxQueue::loop(uint32_t now) {
  const uint32_t seen = _cb_count.load(std::memory_order_acquire);
  uint32_t fresh = seen - _cb_consumed;
  _cb_consumed = seen;

  // Completions owed by abandoned attempts say nothing about the frame in flight.
  while (fresh > 0 && _owed > 0) {
    _owed--;
    fresh--;
  }

  if (_state == STATE_IN_FLIGHT) {
    if (fresh > 0) {
      if (_cb_status.load(std::memory_order_relaxed) != 0) {
        _sent++;
        _pending_len = 0;
        _attempts = 0;
        _state = STATE_IDLE;
      } else {
        scheduleRetry(now);
      }
    } else if (hasElapsed(now, _sent_at + _ack_timeout_ms)) {
      // Never wait forever, or the bridge goes silent. The radio still owes us
      // this attempt's completion, so remember not to believe it later.
      _timeouts++;
      _owed++;
      scheduleRetry(now);
    }
    return;
  }

  if (_state == STATE_IDLE) {
    _pending_len = (uint16_t)_queue.pop(_pending, _queue.maxFrameLen());
    if (_pending_len == 0) return;  // nothing waiting

    _attempts = 0;
    _next_attempt_at = now;
    _state = STATE_PENDING;
  }

  if (_state == STATE_PENDING && hasElapsed(now, _next_attempt_at)) {
    transmit(now);
  }
}

void BridgeTxQueue::transmit(uint32_t now) {
  if (_attempts >= _max_attempts) {
    _failed++;  // out of attempts; drop it so the frames behind it still move
    _pending_len = 0;
    _attempts = 0;
    _state = STATE_IDLE;
    return;
  }

  _attempts++;
  if (_attempts > 1) _retries++;

  if (_sender == nullptr) {
    scheduleRetry(now);
    return;
  }

  _sent_at = now;

  if (_sender->sendFrame(_pending, _pending_len)) {
    _state = STATE_IN_FLIGHT;
  } else {
    // Refused outright, so no completion is coming. Hold the frame and retry.
    _radio_refusals++;
    scheduleRetry(now);
  }
}

void BridgeTxQueue::scheduleRetry(uint32_t now) {
  _next_attempt_at = now + _retry_delay_ms;
  _state = STATE_PENDING;
}

void BridgeTxQueue::reset() {
  // pop() always advances, discarding anything that will not fit, so this drains.
  uint8_t discard[1];
  while (!_queue.isEmpty()) {
    _queue.pop(discard, sizeof(discard));
  }

  // An in-flight send still completes after this returns; do not credit it to
  // whatever the next session sends first.
  if (_state == STATE_IN_FLIGHT) _owed++;

  _pending_len = 0;
  _attempts = 0;
  _state = STATE_IDLE;
}

void BridgeTxQueue::resetStats() {
  _sent = 0;
  _retries = 0;
  _failed = 0;
  _timeouts = 0;
  _radio_refusals = 0;
  _queue.resetStats();
}
