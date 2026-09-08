#pragma once

// LVGL's builtin allocator pool, placed in PSRAM: keeps every widget, image
// decode, and cache allocation out of internal RAM so the Bluetooth stack
// and SD driver always have room for their connection-time bursts.
#include <esp_heap_caps.h>

static inline void* lv_psram_pool_alloc(size_t sz) {
  return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
}
