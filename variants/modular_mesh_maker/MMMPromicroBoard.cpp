#include <Arduino.h>
#include <Wire.h>

#include "MMMPromicroBoard.h"

void MMMPromicroBoard::begin() {
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

    pinMode(P_LORA_POWER_EN, OUTPUT);
    digitalWrite(P_LORA_POWER_EN, HIGH);
    delay(10);
}
