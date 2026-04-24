#ifdef IKOKA_NRF52

#include <Arduino.h>
#include <Wire.h>

#include "IkokaNrf52Board.h"

void IkokaNrf52Board::begin() {
  NRF52Board::begin();

  // ensure we have pull ups on the screen i2c, this isn't always available
  // in hardware and it should only be 20k ohms. Disable the pullups if we
  // are using the rotated lcd breakout board
  #if defined(DISPLAY_CLASS) && DISPLAY_ROTATION == 0
    pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
    pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
  #endif

  pinMode(PIN_VBAT, INPUT);
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, HIGH);

  // required button pullup is handled as part of button initilization
  // in target.cpp

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

  delay(10); // give sx1262 some time to power up

  // Start LEDs with defaults; prefs are applied after loadPrefs()
  static LEDManager _ledManager(LED_GREEN, LED_RED, false, false);
  ledManager = &_ledManager;
  ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}

#endif
