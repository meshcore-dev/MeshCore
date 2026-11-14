#include "LilkaBoard.h"
#include <Arduino.h>
#include <Wire.h>
extern "C" bool verifyRollbackLater() {
    return true;
}

void LilkaBoard::begin() {
  ESP32Board::begin();
  #ifdef DISPLAY_CLASS
    btn_prev_state = HIGH;
    pinMode(PIN_USER_BTN, INPUT_PULLUP);
    pinMode(JOYSTICK_LEFT, INPUT_PULLUP);
    pinMode(JOYSTICK_RIGHT, INPUT_PULLUP);
    pinMode(PIN_BACK_BTN, INPUT_PULLUP);
  #endif
}

uint16_t LilkaBoard::getBattMilliVolts() {
#ifdef PIN_VBAT_READ
  analogReadResolution(12);

  uint32_t raw = 0;
  for (int i = 0; i < 4; i++) {
    raw += analogReadMilliVolts(PIN_VBAT_READ);
  }
  raw = raw / 4;

  return (ADC_MULTIPLIER * raw);
#else
  return 0; // not supported
#endif
}

const char *LilkaBoard::getManufacturerName() const {
  return "Lilka V2.3";
}
