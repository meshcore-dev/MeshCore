#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

#include "PromicroBoard.h"

void PromicroBoard::loop() {
#ifdef NRF52_POWER_MANAGEMENT
  const uint32_t now = millis();
  if ((uint32_t)(now - last_battery_check) < PWRMGT_BATTERY_CHECK_INTERVAL_MS) return;
  last_battery_check = now;

  if (isExternalPowered()) {
    low_battery_samples = 0;
    return;
  }

  // Require consecutive low readings to reject brief load transients.
  const uint16_t mv = getBattMilliVolts();
  if (mv >= PWRMGT_VOLTAGE_SHUTDOWN) {
    low_battery_samples = 0;
  } else if (++low_battery_samples >= PWRMGT_LOW_BATTERY_SAMPLES) {
    shutdownPeripherals();
    initiateShutdown(SHUTDOWN_REASON_LOW_VOLTAGE);
  }
#endif
}

void PromicroBoard::powerOff() {
#ifdef NRF52_POWER_MANAGEMENT
  shutdownPeripherals();
  initiateShutdown(SHUTDOWN_REASON_USER);
#else
  NRF52Board::powerOff();
#endif
}

#ifdef NRF52_POWER_MANAGEMENT
// Static configuration for power management
// Values come from variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void PromicroBoard::initiateShutdown(uint8_t reason) {
  // Disconnect signal drivers before removing the external radio supply.
  // SYSTEMOFF retains GPIO configuration: NSS/RESET left high can otherwise
  // feed the unpowered module through its signal pins.
  // Boot protection runs before SPI is initialized; uninit is invalid there.
  if (reason != SHUTDOWN_REASON_BOOT_PROTECT) SPI.end();
  const uint8_t radio_pins[] = {
    P_LORA_NSS, P_LORA_RESET, P_LORA_SCLK, P_LORA_MOSI,
    P_LORA_MISO, P_LORA_BUSY, P_LORA_DIO_1, SX126X_RXEN
  };
  for (uint8_t pin : radio_pins) {
    pinMode(pin, INPUT);  // High impedance, no pull-up or pull-down.
  }
  // Preload the low latch before enabling the output, including boot lockout.
  digitalWrite(SX126X_POWER_EN, LOW);
  pinMode(SX126X_POWER_EN, OUTPUT);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void PromicroBoard::begin() {    
    NRF52Board::begin();
  
    pinMode(PIN_VBAT_READ, INPUT);

    #ifdef BUTTON_PIN
      pinMode(BUTTON_PIN, INPUT_PULLUP);
    #endif

    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
    #endif
    
    Wire.begin();

    pinMode(SX126X_POWER_EN, OUTPUT);
    #ifdef NRF52_POWER_MANAGEMENT
      checkBootVoltage(&power_config);
    #endif
    digitalWrite(SX126X_POWER_EN, HIGH);
    delay(10);   // give sx1262 some time to power up
}
