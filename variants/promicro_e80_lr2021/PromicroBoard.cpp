#include <Arduino.h>
#include <Wire.h>

#include "PromicroBoard.h"

void PromicroBoard::begin() {    
    NRF52Board::begin();
    btn_prev_state = HIGH;
  
    pinMode(PIN_VBAT_READ, INPUT);

    #ifdef BUTTON_PIN
      pinMode(BUTTON_PIN, INPUT_PULLUP);
    #endif

    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
    #endif
    
    Wire.begin();

    pinMode(EXT_VCC_EN, OUTPUT);
    digitalWrite(EXT_VCC_EN, HIGH);
    delay(10);   // give module some time to power up
}