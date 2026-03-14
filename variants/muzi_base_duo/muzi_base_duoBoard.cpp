#include <Arduino.h>
#include <Wire.h>

#include "muzi_base_duoBoard.h"

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void muzi_base_duoBoard::initiateShutdown(uint8_t reason) {
  // Disable LoRa module power before shutdown
  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void muzi_base_duoBoard::begin() {
  NRF52BoardDCDC::begin();

  pinMode(PIN_VBAT_READ, INPUT);

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif
#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
#endif
  delay(10);   // give sx1262 some time to power up
}