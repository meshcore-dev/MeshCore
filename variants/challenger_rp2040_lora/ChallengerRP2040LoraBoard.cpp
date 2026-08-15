#include "ChallengerRP2040LoraBoard.h"

#include <Arduino.h>
#include <Wire.h>

void ChallengerRP2040LoraBoard::begin() {
  startup_reason = BD_STARTUP_NORMAL;

  pinMode(PIN_LED, OUTPUT);

  Wire.begin();
  delay(10); // give sx1276 some time to power up
}
