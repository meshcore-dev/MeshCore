#pragma once

#include <stdint.h>

// LED behaviour from the Elecrow ThinkNode M6 manual.
#define M6_BOOT_LED_MILLIS        10000   // red on for 10 s at power-on
#define M6_BATT_LOW_MV            3500    // low-battery alert at 3.5 V or less
#define M6_LOW_BATT_PERIOD_MILLIS 5000    // slow flashing at 0.2 Hz
#define M6_LOW_BATT_FLASH_MILLIS  500     // our choice: the manual gives no duty cycle
#define M6_DATA_BLINK_MILLIS      100     // rapid flashing at 5 Hz: 100 ms on, 100 ms off
#define M6_HEARTBEAT_MILLIS       3000    // not in the manual: a short sign of life on battery
#define M6_HEARTBEAT_FLASH_MILLIS 2

// Red PWR LED: on for 10 s at power-on, steady while charging, slow flashing
// when the battery is low, otherwise a 2 ms flash every 3 s.
inline bool m6RedLed(uint32_t now, bool charging, uint16_t batt_mv) {
  if (now < M6_BOOT_LED_MILLIS) {
    return true;
  }
  if (charging) {
    return true;
  }
  bool low_battery = batt_mv > 1000 && batt_mv <= M6_BATT_LOW_MV;   // below 1 V is a bad reading
  if (low_battery) {
    return now % M6_LOW_BATT_PERIOD_MILLIS < M6_LOW_BATT_FLASH_MILLIS;
  }
  return now % M6_HEARTBEAT_MILLIS < M6_HEARTBEAT_FLASH_MILLIS;
}

// Blue DATA LED: rapid flashing while LoRa data is sent or received,
// starting with a flash.
inline bool m6BlueLed(bool data_active, uint32_t data_elapsed_ms) {
  if (!data_active) {
    return false;
  }
  return (data_elapsed_ms / M6_DATA_BLINK_MILLIS) % 2 == 0;
}
