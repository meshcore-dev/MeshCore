#include <Arduino.h>
#include <Wire.h>

#include "ThinkNodeM1Board.h"

#ifdef THINKNODE_M1

void ThinkNodeM1Board::begin() {
  NRF52Board::begin();

  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10); // give sx1262 some time to power up

  // Start LEDs with defaults; prefs are applied after loadPrefs()
  // LED_GREEN is active-LOW (LED_STATE_ON=LOW), P_LORA_TX_LED is active-HIGH
  static LEDManager _ledManager(LED_GREEN, P_LORA_TX_LED, false, true);
  ledManager = &_ledManager;
  ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}

uint16_t ThinkNodeM1Board::getBattMilliVolts() {
  int adcvalue = 0;

  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(10);

  // ADC range is 0..3000mV and resolution is 12-bit (0..4095)
  adcvalue = analogRead(PIN_VBAT_READ);
  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  return (uint16_t)((float)adcvalue * REAL_VBAT_MV_PER_LSB);
}
#endif
