#include <Arduino.h>
#include <Wire.h>

#include "TechoBoard.h"

#ifdef LILYGO_TECHO

void TechoBoard::begin() {
  NRF52Board::begin();

  // Configure battery measurement control BEFORE Wire.begin()
  // to ensure P0.02 is not claimed by another peripheral
  pinMode(PIN_VBAT_MEAS_EN, OUTPUT);
  digitalWrite(PIN_VBAT_MEAS_EN, LOW);
  pinMode(PIN_VBAT_READ, INPUT);

  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);
}

uint16_t TechoBoard::getBattMilliVolts() {
  // Use LilyGo's exact ADC configuration
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);

  // Enable battery voltage divider (MOSFET gate on P0.31)
  pinMode(PIN_VBAT_MEAS_EN, OUTPUT);
  digitalWrite(PIN_VBAT_MEAS_EN, HIGH);

  // Reclaim P0.02 for analog input (in case another peripheral touched it)
  pinMode(PIN_VBAT_READ, INPUT);
  delay(10);  // let divider + ADC settle

  // Read and average (matching LilyGo's approach)
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(PIN_VBAT_READ);
    delayMicroseconds(100);
  }
  uint16_t adc = sum / 8;

  // Disable divider to save power
  digitalWrite(PIN_VBAT_MEAS_EN, LOW);

  // LilyGo's exact formula: adc * (3000.0 / 4096.0) * 2.0
  // = adc * 0.73242188 * 2.0 = adc * 1.46484375
  uint16_t millivolts = (uint16_t)((float)adc * (3000.0f / 4096.0f) * 2.0f);

  return millivolts;
}
#endif