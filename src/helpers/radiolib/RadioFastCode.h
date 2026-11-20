#pragma once

// Board opt-in for task-context retuning. Calls into unannotated RadioLib or
// SDK code can still access flash, so this does not make the path ISR-safe.
#if defined(ESP32_PLATFORM) && defined(MC_ESP32_RADIO_FAST_CODE) && MC_ESP32_RADIO_FAST_CODE
#include <esp_attr.h>
// Xtensa builds also use -mtext-section-literals to place nearby literals
// in the same section. Avoid inlining the functions back into flash callers.
#define MC_RADIO_FAST_CODE_ATTR IRAM_ATTR __attribute__((noinline))
#else
#define MC_RADIO_FAST_CODE_ATTR
#endif
