#include "T114Board.h"

#include <Arduino.h>
#include <Wire.h>

#ifdef NRF52_POWER_MANAGEMENT
// Static configuration for power management
// Values come from variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel   = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel_liion  = PWRMGT_LPCOMP_REFSEL_LIION,
  .lpcomp_refsel_lfp    = PWRMGT_LPCOMP_REFSEL_LFP,
  .lpcomp_refsel_lto    = PWRMGT_LPCOMP_REFSEL_LTO,
  .voltage_bootlock_liion = PWRMGT_VOLTAGE_BOOTLOCK_LIION,
  .voltage_bootlock_lfp   = PWRMGT_VOLTAGE_BOOTLOCK_LFP,
  .voltage_bootlock_lto   = PWRMGT_VOLTAGE_BOOTLOCK_LTO
};

void T114Board::initiateShutdown(uint8_t reason) {
#if ENV_INCLUDE_GPS == 1
  pinMode(GPS_EN, OUTPUT);
  digitalWrite(GPS_EN, LOW);
#endif
  digitalWrite(SX126X_POWER_EN, LOW);

  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);
  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, enable_lpcomp ? HIGH : LOW);

  if (enable_lpcomp) {
    configureVoltageWake(power_config.lpcomp_ain_channel,
                         getRefselForChemistry(battery_chem, &power_config));
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void T114Board::begin() {
  NRF52Board::begin();

  pinMode(PIN_VBAT_READ, INPUT);

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, HIGH);
#endif

  pinMode(SX126X_POWER_EN, OUTPUT);
#ifdef NRF52_POWER_MANAGEMENT
  // Boot voltage protection check (may not return if voltage too low)
  // We need to call this after we configure SX126X_POWER_EN as output but before we pull high
  checkBootVoltage(&power_config);
#endif
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10); // give sx1262 some time to power up
}