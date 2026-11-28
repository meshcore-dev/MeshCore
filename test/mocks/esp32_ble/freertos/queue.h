#pragma once
#include <deque>
#include <vector>
#include <cstring>
struct StaticQueue_t { size_t size, capacity; std::deque<std::vector<uint8_t>> frames; };
using QueueHandle_t = StaticQueue_t*;
inline QueueHandle_t xQueueCreateStatic(size_t count, size_t size, uint8_t*, StaticQueue_t* queue) {
  queue->size = size; queue->capacity = count; return queue;
}
inline void xQueueReset(QueueHandle_t queue) { queue->frames.clear(); }
inline int xQueueSend(QueueHandle_t queue, const void* frame, int) {
  if (queue->frames.size() == queue->capacity) return 0;
  const auto* bytes = static_cast<const uint8_t*>(frame);
  queue->frames.emplace_back(bytes, bytes + queue->size); return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t queue, void* frame, int) {
  if (queue->frames.empty()) return 0;
  memcpy(frame, queue->frames.front().data(), queue->size); queue->frames.pop_front(); return pdTRUE;
}
