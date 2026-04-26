#include <Arduino.h>
#include <Wire.h>

#include "MinewsemiME25LS01Board.h"

void MinewsemiME25LS01Board::begin() {
  NRF52Board::begin();
  btn_prev_state = HIGH;
  
  pinMode(PIN_VBAT_READ, INPUT);

#ifdef BUTTON_PIN
  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();
  
#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

  delay(10);   // give sx1262 some time to power up
}