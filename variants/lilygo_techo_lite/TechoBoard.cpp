#include <Arduino.h>
#include <Wire.h>

#include "TechoBoard.h"

#ifdef LILYGO_TECHO

void TechoBoard::begin() {
  NRF52Board::begin();

  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up
}

uint16_t TechoBoard::getBattMilliVolts() {
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

void TechoBoard::setRfSwitchTx(bool tx) {
  if (tx) { // TX
    digitalWrite(S68F_RF_VC1, HIGH);
    digitalWrite(S68F_RF_VC2, LOW);
  } else { // RX
    digitalWrite(S68F_RF_VC1, LOW);
    digitalWrite(S68F_RF_VC2, HIGH);
  }
}

void TechoBoard::onBeforeTransmit() {
  // RF switching NOT handled by RadioLib via SX126X_DIO2_AS_RF_SWITCH and setRfSwitchPins()
#if defined(P_LORA_TX_LED)
  digitalWrite(P_LORA_TX_LED, LOW);  // TX LED on
#endif
  TechoBoard::setRfSwitchTx(true);
}

void TechoBoard::onAfterTransmit() {
  TechoBoard::setRfSwitchTx(false);
#if defined(P_LORA_TX_LED)
  digitalWrite(P_LORA_TX_LED, HIGH);   // TX LED off
#endif
}

#endif
