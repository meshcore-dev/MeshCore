#pragma once

#include <stdint.h>

/**
 * @brief Maps the `bridge.channel` pref onto an nRF RADIO frequency.
 *
 * Kept free of Arduino/nRF dependencies so it can be unit tested natively.
 *
 * The pref is shared with the ESP-NOW bridge and keeps its Wi-Fi 1-14 numbering,
 * so `set bridge.channel 6` means 2437 MHz on either platform. That leaves the
 * CLI validation, the persisted layout and the docs untouched.
 */
class NRFRadioChannel {
public:
  /**
   * @brief Wi-Fi channel number to RADIO FREQUENCY register value.
   *
   * A channel outside 1-14 is refused rather than corrected, so that the bridge
   * declines to start on it exactly as the ESP-NOW bridge does when
   * esp_wifi_set_channel() rejects the same value. The case that matters is 0,
   * which is what a pref saved before bridge_channel existed reads back as.
   *
   * @param wifi_channel 1-14
   * @return MHz above 2400, within RADIO's 0-100 range, or -1 if out of range
   */
  static int frequencyFor(uint8_t wifi_channel);
};
