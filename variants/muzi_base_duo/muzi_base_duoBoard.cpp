#include <Arduino.h>
#include <Wire.h>

#include "muzi_base_duoBoard.h"

// void muzi_base_duoBoard::begin() {
//   NRF52BoardDCDC::begin();
//   btn_prev_state = HIGH;

// #ifdef BUTTON_PIN
//   pinMode(BATTERY_PIN, INPUT);
//   pinMode(BUTTON_PIN, INPUT);
//   pinMode(LED_PIN, OUTPUT);
// #endif

// #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
//   Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
// #endif

//   Wire.begin();

//   delay(10);   // give sx1262 some time to power up
// }
void muzi_base_duoBoard::begin() {
  NRF52BoardDCDC::begin();
  pinMode(BATTERY_PIN, INPUT);
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT);
#endif

#ifdef PIN_USER_BTN_ANA
  pinMode(PIN_USER_BTN_ANA, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

#ifdef NRF52_POWER_MANAGEMENT
  // Boot voltage protection check (may not return if voltage too low)
  // We need to call this after we configure SX126X_POWER_EN as output but before we pull high
  checkBootVoltage(&power_config);
#endif
}