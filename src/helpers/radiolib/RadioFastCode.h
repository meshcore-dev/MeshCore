#pragma once

// All embedded targets run the shared radio-profile transaction from internal
// RAM. The ARM linker scripts collect these input sections in initialized
// RAM, which the core copies from flash before setup(). Use separate sections
// so the linker can discard unused functions. This is task-context code.
#if defined(ESP32_PLATFORM)
#define MC_RADIO_RETUNE_ATTR(name) __attribute__((section(".iram1.radio_" #name), noinline))
#elif defined(NRF52_PLATFORM)
#define MC_RADIO_RETUNE_ATTR(name) __attribute__((section(".data.radio_" #name), noinline))
#elif defined(STM32_PLATFORM)
#define MC_RADIO_RETUNE_ATTR(name) __attribute__((section(".RamFunc.radio_" #name), noinline))
#elif defined(RP2040_PLATFORM)
#define MC_RADIO_RETUNE_ATTR(name) __attribute__((section(".time_critical.radio_" #name), noinline))
#else
#define MC_RADIO_RETUNE_ATTR(name)
#endif

// Boards with room can also place chip-specific helpers in instruction RAM.
#if defined(ESP32_PLATFORM) && defined(MC_ESP32_RADIO_FAST_CODE) && MC_ESP32_RADIO_FAST_CODE
#include <esp_attr.h>
// Xtensa builds also use -mtext-section-literals to place nearby literals
// in the same section. Avoid inlining the functions back into flash callers.
#define MC_RADIO_FAST_CODE_ATTR IRAM_ATTR __attribute__((noinline))
#else
#define MC_RADIO_FAST_CODE_ATTR
#endif
