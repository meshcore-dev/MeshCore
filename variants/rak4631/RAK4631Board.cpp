#include <Arduino.h>
#include <Wire.h>

#include "RAK4631Board.h"

#ifdef BOOT_DIAG
// Diagnostic LED blink: N short blinks on green LED to indicate boot stage
static void diag_blink(uint8_t count) {
  pinMode(PIN_LED1, OUTPUT);
  for (uint8_t i = 0; i < count; i++) {
    digitalWrite(PIN_LED1, HIGH);
    delay(100);
    digitalWrite(PIN_LED1, LOW);
    delay(150);
  }
  delay(300);
}
#else
#define diag_blink(n) ((void)0)
#endif

#ifdef NRF52_POWER_MANAGEMENT
// Static configuration for power management
// Values set in variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void RAK4631Board::initiateShutdown(uint8_t reason) {
  // Disable LoRa module power before shutdown
  digitalWrite(SX126X_POWER_EN, LOW);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    if (!configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel)) {
      NVIC_SystemReset();
    }
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void RAK4631Board::begin() {
  diag_blink(1);  // Stage 1: entering begin()

#ifdef DISABLE_DCDC
  NRF52Board::begin();
#else
  NRF52BoardDCDC::begin();
#endif

  diag_blink(2);  // Stage 2: board base init done

  pinMode(PIN_VBAT_READ, INPUT);
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#ifdef PIN_USER_BTN_ANA
  pinMode(PIN_USER_BTN_ANA, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  diag_blink(3);  // Stage 3: I2C done, about to configure LoRa power

  pinMode(SX126X_POWER_EN, OUTPUT);
#if defined(NRF52_POWER_MANAGEMENT) && !defined(DISABLE_BOOT_PROTECTION)
  // Boot voltage protection check (may not return if voltage too low)
  // We need to call this after we configure SX126X_POWER_EN as output but before we pull high
  checkBootVoltage(&power_config);
#endif

  diag_blink(4);  // Stage 4: power check passed

  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up

  diag_blink(5);  // Stage 5: board.begin() complete
}