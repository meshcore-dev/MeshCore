#include <Arduino.h>
#include "ThinkNodeM3Board.h"
#include <Wire.h>

#include <bluefruit.h>

void ThinkNodeM3Board::begin() {
  NRF52Board::begin();
  btn_prev_state = HIGH;

  Wire.begin();

  delay(10);   // give sx1262 some time to power up

  // Start LEDs with defaults; prefs are applied after loadPrefs()
  static LEDManager _ledManager(PIN_LED_GREEN, PIN_LED_BLUE, false, false);
  ledManager = &_ledManager;
  ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}

uint16_t ThinkNodeM3Board::getBattMilliVolts() {
  int adcvalue = 0;

  analogReference(AR_INTERNAL_2_4);
  analogReadResolution(ADC_RESOLUTION);
  delay(10);

  // ADC range is 0..2400mV and resolution is 12-bit (0..4095)
  adcvalue = analogRead(PIN_VBAT_READ);
  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  return (uint16_t)((float)adcvalue * ADC_FACTOR);
}
