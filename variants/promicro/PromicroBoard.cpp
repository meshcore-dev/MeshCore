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

    pinMode(SX126X_POWER_EN, OUTPUT);
    digitalWrite(SX126X_POWER_EN, HIGH);
    delay(10);   // give sx1262 some time to power up

    // Start LEDs with defaults; prefs are applied after loadPrefs()
    static LEDManager _ledManager(PIN_LED, -1);
    ledManager = &_ledManager;
    ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}