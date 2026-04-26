#ifdef XIAO_NRF52

#include <Arduino.h>
#include <Wire.h>

#include "XiaoNrf52Board.h"

#ifdef NRF52_POWER_MANAGEMENT
// Static configuration for power management
// Values set in variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void XiaoNrf52Board::initiateShutdown(uint8_t reason) {
  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);

  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, enable_lpcomp ? LOW : HIGH);

  if (enable_lpcomp) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void XiaoNrf52Board::begin() {
  NRF52BoardDCDC::begin();

  // Configure battery voltage ADC
  pinMode(PIN_VBAT, INPUT);
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);  // Enable VBAT divider for reading
  analogReadResolution(12);
  analogReference(AR_INTERNAL_3_0);
  delay(50);  // Allow ADC to settle

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, HIGH);
#endif

#ifdef NRF52_POWER_MANAGEMENT
  // Boot voltage protection check (may not return if voltage too low)
  checkBootVoltage(&power_config);
#endif

  delay(10);  // Give sx1262 some time to power up
}

uint16_t XiaoNrf52Board::getBattMilliVolts() {
  // https://wiki.seeedstudio.com/XIAO_BLE#q3-what-are-the-considerations-when-using-xiao-nrf52840-sense-for-battery-charging
  // VBAT_ENABLE must be LOW to read battery voltage
  digitalWrite(VBAT_ENABLE, LOW);
  int adcvalue = analogRead(PIN_VBAT);
  return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
}

#endif