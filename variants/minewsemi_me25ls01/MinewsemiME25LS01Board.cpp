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
  
  delay(10);   // give sx1262 some time to power up

  // Start LEDs with defaults; prefs are applied after loadPrefs()
  static LEDManager _ledManager(LED_BLUE, LED_RED);
  ledManager = &_ledManager;
  ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}