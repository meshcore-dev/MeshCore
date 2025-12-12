#include <Arduino.h>
#include <Wire.h>

#include "SenseCapSolarBoard.h"

void SenseCapSolarBoard::begin() {
  NRF52Board::begin();

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

  delay(10);   // give sx1262 some time to power up
}