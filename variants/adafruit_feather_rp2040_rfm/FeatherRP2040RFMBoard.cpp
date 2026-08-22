#include "FeatherRP2040RFMBoard.h"

#include <Arduino.h>
#include <Wire.h>

void FeatherRP2040RFMBoard::begin() {
  startup_reason = BD_STARTUP_NORMAL;

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
#endif

#ifdef PIN_VBAT_READ
  pinMode(PIN_VBAT_READ, INPUT);
#endif

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setSDA(PIN_BOARD_SDA);
  Wire.setSCL(PIN_BOARD_SCL);
#endif

  Wire.begin();   // STEMMA QT connector

  delay(10);   // give the RFM95 some time to power up
}

bool FeatherRP2040RFMBoard::startOTAUpdate(const char* id, char reply[]) {
  return false;
}
