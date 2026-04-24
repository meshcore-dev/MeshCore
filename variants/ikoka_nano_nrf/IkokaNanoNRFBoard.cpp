#ifdef XIAO_NRF52

#include <Arduino.h>
#include <Wire.h>

#include "IkokaNanoNRFBoard.h"

void IkokaNanoNRFBoard::begin() {
  NRF52Board::begin();

  pinMode(PIN_VBAT, INPUT);
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, HIGH);

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

//  pinMode(SX126X_POWER_EN, OUTPUT);
//  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up

  // Start LEDs with defaults; prefs are applied after loadPrefs()
  static LEDManager _ledManager(LED_GREEN, LED_RED, false, false);
  ledManager = &_ledManager;
  ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}

#endif
