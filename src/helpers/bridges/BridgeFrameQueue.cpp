#include "BridgeFrameQueue.h"

#include <string.h>

namespace {

/**
 * Head and tail run over [0, 2*capacity), not the whole uint32_t range: reducing
 * a free-running counter modulo a non-power-of-two capacity would alias two
 * slots onto each other at the 2^32 wrap.
 */
inline uint32_t ringSpan(uint8_t capacity) {
  return 2u * (uint32_t)capacity;
}

inline uint32_t ringCount(uint32_t head, uint32_t tail, uint8_t capacity) {
  const uint32_t span = ringSpan(capacity);
  return (head + span - tail) % span;
}

}  // namespace

BridgeFrameQueue::BridgeFrameQueue(uint8_t capacity, uint16_t max_frame_len)
    : _capacity(capacity < 1 ? 1 : capacity), _max_frame_len(max_frame_len), _slots(nullptr),
      _lengths(nullptr), _head(0), _tail(0), _pushed(0), _dropped_full(0),
      _dropped_oversize_in(0), _high_water(0), _popped(0), _dropped_oversize_out(0) {
  _slots = new uint8_t[(size_t)_capacity * (size_t)_max_frame_len];
  _lengths = new uint16_t[_capacity];
}

BridgeFrameQueue::~BridgeFrameQueue() {
  delete[] _slots;
  delete[] _lengths;
}

bool BridgeFrameQueue::push(const uint8_t *frame, size_t len) {
  if (frame == nullptr || len == 0 || len > _max_frame_len) {
    _dropped_oversize_in.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  const uint32_t head = _head.load(std::memory_order_relaxed);
  const uint32_t tail = _tail.load(std::memory_order_acquire);

  const uint32_t depth = ringCount(head, tail, _capacity);
  if (depth >= _capacity) {
    _dropped_full.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  const uint32_t slot = head % _capacity;
  memcpy(_slots + (size_t)slot * (size_t)_max_frame_len, frame, len);
  _lengths[slot] = (uint16_t)len;

  // release: the frame bytes must be visible before the consumer sees the new head
  _head.store((head + 1) % ringSpan(_capacity), std::memory_order_release);

  _pushed.fetch_add(1, std::memory_order_relaxed);
  if (depth + 1 > _high_water.load(std::memory_order_relaxed)) {
    _high_water.store((uint8_t)(depth + 1), std::memory_order_relaxed);
  }
  return true;
}

size_t BridgeFrameQueue::pop(uint8_t *out, size_t out_cap) {
  const uint32_t tail = _tail.load(std::memory_order_relaxed);
  const uint32_t head = _head.load(std::memory_order_acquire);

  if (head == tail) return 0;

  const uint32_t slot = tail % _capacity;
  const uint16_t len = _lengths[slot];

  size_t written = 0;
  if (out == nullptr || len > out_cap) {
    // Drop it: leaving it at the head would stall every frame behind it for good.
    _dropped_oversize_out++;
  } else {
    memcpy(out, _slots + (size_t)slot * (size_t)_max_frame_len, len);
    written = len;
    _popped++;
  }

  // release: the slot is only reusable once we are done reading it
  _tail.store((tail + 1) % ringSpan(_capacity), std::memory_order_release);

  return written;
}

void BridgeFrameQueue::clear() {
  // release: matches pop(), so the producer only reuses slots we are done reading
  _tail.store(_head.load(std::memory_order_acquire), std::memory_order_release);
}

bool BridgeFrameQueue::isEmpty() const {
  return _head.load(std::memory_order_acquire) == _tail.load(std::memory_order_acquire);
}

uint8_t BridgeFrameQueue::count() const {
  const uint32_t head = _head.load(std::memory_order_acquire);
  const uint32_t tail = _tail.load(std::memory_order_acquire);
  return (uint8_t)ringCount(head, tail, _capacity);
}

void BridgeFrameQueue::resetStats() {
  _pushed.store(0, std::memory_order_relaxed);
  _dropped_full.store(0, std::memory_order_relaxed);
  _dropped_oversize_in.store(0, std::memory_order_relaxed);
  _high_water.store(0, std::memory_order_relaxed);
  _popped = 0;
  _dropped_oversize_out = 0;
}
