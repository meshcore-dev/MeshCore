#include "ChallengerBoard.h"

#include <Arduino.h>
#include <Wire.h>

void ChallengerBoard::begin() {
  // for future use, sub-classes SHOULD call this from their begin()
  startup_reason = BD_STARTUP_NORMAL;

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setSDA(PIN_BOARD_SDA);
  Wire.setSCL(PIN_BOARD_SCL);
#endif

  Wire.begin();

  delay(10); // give the RFM95W some time to power up
}
