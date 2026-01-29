#include <Arduino.h>
#include <Wire.h>

#include "RAK3401Board.h"

void RAK3401Board::begin() {
  NRF52BoardDCDC::begin();
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

  pinMode(PIN_3V3_EN, OUTPUT);
  digitalWrite(PIN_3V3_EN, HIGH);

#ifdef P_LORA_PA_EN
  // Initialize RAK13302 1W LoRa transceiver module PA control pin
  pinMode(P_LORA_PA_EN, OUTPUT);
  digitalWrite(P_LORA_PA_EN, LOW);  // Start with PA disabled
  delay(10);  // Allow PA module to initialize
#endif
}