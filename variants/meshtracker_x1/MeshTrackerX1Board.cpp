#include <Arduino.h>
#include <Wire.h>

#include "MeshTrackerX1Board.h"

void MeshTrackerX1Board::begin() {
  NRF52BoardDCDC::begin();
  btn_prev_state = LOW;   // button is active HIGH

#ifdef BUTTON_PIN
  pinMode(BATTERY_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(LED_PIN, OUTPUT);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  delay(10);   // give lr2021 some time to power up
}
