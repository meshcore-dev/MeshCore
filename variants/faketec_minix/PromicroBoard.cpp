#include <Arduino.h>
#include <Wire.h>

#include "PromicroBoard.h"
#include "target.h"

void PromicroBoard::begin() {    
    NRF52Board::begin();
    btn_prev_state = HIGH;
  
    pinMode(PIN_VBAT_READ, INPUT);

    #ifdef UI_HAS_JOYSTICK
      pinMode(PIN_BACK_BTN, INPUT_PULLUP);
      pinMode(JOYSTICK_LEFT, INPUT_PULLUP);
      pinMode(JOYSTICK_RIGHT, INPUT_PULLUP);
      pinMode(PIN_USER_BTN, INPUT_PULLUP);
      joystick_left.begin();
      joystick_right.begin();
      user_btn.begin();
      back_btn.begin();
    #else if defined (BUTTON_PIN)
      pinMode(BUTTON_PIN, INPUT_PULLUP);
    #endif

    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
    #endif
    
    Wire.begin();

    pinMode(SX126X_POWER_EN, OUTPUT);
    digitalWrite(SX126X_POWER_EN, HIGH);
    delay(10);   // give sx1262 some time to power up
}