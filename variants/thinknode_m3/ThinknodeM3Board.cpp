#include <Arduino.h>
#include "ThinknodeM3Board.h"
#include <Wire.h>

#include <bluefruit.h>

void ThinknodeM3Board::begin() {
  Nrf52BoardDCDC::begin();
  btn_prev_state = HIGH;

  Wire.begin();

  delay(10);   // give sx1262 some time to power up
}