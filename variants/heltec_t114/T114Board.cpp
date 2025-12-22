#include "T114Board.h"

#include <Arduino.h>
#include <Wire.h>

void T114Board::begin() {
  NRF52Board::begin();
  NRF_POWER->DCDCEN = 1;

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
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10); // give sx1262 some time to power up
}