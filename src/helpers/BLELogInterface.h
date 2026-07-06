#pragma once

/**
 * Platform-selecting shim for BLELogInterface.
 * Include this header and use BLELogInterface directly; the correct
 * platform implementation is pulled in automatically.
 *
 * Supported platforms: ESP32, nRF52.
 * On unsupported platforms this header intentionally defines nothing —
 * guard usage with #if defined(NRF52_PLATFORM) || defined(ESP32).
 */

#if defined(NRF52_PLATFORM)
  #include "nrf52/BLELogInterface.h"
#elif defined(ESP32)
  #include "esp32/BLELogInterface.h"
#endif
