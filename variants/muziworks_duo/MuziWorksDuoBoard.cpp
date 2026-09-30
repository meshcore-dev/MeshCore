#ifdef MUZIWORKS_DUO

#include "MuziWorksDuoBoard.h"

#include <Arduino.h>
#include <Wire.h>

void MuziWorksDuoBoard::begin() {
  NRF52BoardDCDC::begin();

  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // LEDs off (active LOW)
  digitalWrite(LED_GREEN, !LED_STATE_ON);
  digitalWrite(LED_BLUE, LED_STATE_ON);

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

  delay(10);
}

#endif
