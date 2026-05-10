#include <Arduino.h>
#include <Wire.h>

#include "GAT562MeshTrackerProBoard.h"


#ifdef NRF52_POWER_MANAGEMENT
// Static configuration for power management
// Values set in variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};


void GAT562MeshTrackerProBoard::initiateShutdown(uint8_t reason) {
  // Disable LoRa module power before shutdown
  digitalWrite(SX126X_POWER_EN, LOW);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT


void GAT562MeshTrackerProBoard::begin() {
  NRF52BoardDCDC::begin();
  pinMode(PIN_VBAT_READ, INPUT);

  // Set all button pins to INPUT_PULLUP
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_BUTTON4, INPUT_PULLUP);
  pinMode(PIN_BUTTON5, INPUT_PULLUP);
  pinMode(PIN_BUTTON6, INPUT_PULLUP);

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

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