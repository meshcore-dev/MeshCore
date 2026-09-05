#pragma once

#include <stddef.h>

// Small ownership helpers for MQTT runtime buffers. They intentionally keep
// each buffer independent: a failed allocation leaves that buffer null (so its
// caller can use its stack fallback) without discarding the other buffers.
namespace MQTTRuntimeBufferLifecycle {

template <typename Allocator>
inline void* allocateIfMissing(void* buffer, size_t size, Allocator allocate) {
  return buffer != nullptr ? buffer : allocate(size);
}

template <typename Deallocator>
inline void* release(void* buffer, Deallocator deallocate) {
  if (buffer != nullptr) {
    deallocate(buffer);
  }
  return nullptr;
}

}  // namespace MQTTRuntimeBufferLifecycle
