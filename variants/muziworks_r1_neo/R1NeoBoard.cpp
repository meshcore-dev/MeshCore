#include <Arduino.h>
#include <Wire.h>

#include "R1NeoBoard.h"

#ifdef NRF52_POWER_MANAGEMENT
// Static configuration for power management
// Values set in variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void R1NeoBoard::initiateShutdown(uint8_t reason) {
  // Disable LoRa module power before shutdown
  digitalWrite(SX126X_POWER_EN, LOW);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void R1NeoBoard::begin() {
  // R1 Neo peculiarity: tell DCDC converter to stay powered.
  // Must be done as soon as practical during boot.

  pinMode(PIN_DCDC_EN_MCU_HOLD, OUTPUT);
  digitalWrite(PIN_DCDC_EN_MCU_HOLD, HIGH);

  // R1 Neo peculiarity: Tell I/O Controller device is on
  // Enables passthrough of buttons and LEDs

  pinMode(PIN_SOFT_SHUTDOWN, OUTPUT);
  digitalWrite(PIN_SOFT_SHUTDOWN, HIGH);

  NRF52BoardDCDC::begin();

  // button is active high and passed through from I/O controller
  pinMode(PIN_USER_BTN, INPUT);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // battery pins
  pinMode(PIN_BAT_CHG, INPUT);
  pinMode(PIN_VBAT_READ, INPUT);

  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);

  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
#ifdef NRF52_POWER_MANAGEMENT
  // Boot voltage protection check (may not return if voltage too low)
  // We need to call this after we configure SX126X_POWER_EN as output but before we pull high
  checkBootVoltage(&power_config);
#endif
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up
}
