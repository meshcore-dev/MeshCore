#include <Arduino.h>
#include <Wire.h>

#include "RAKWismeshTagBoard.h"

void RAKWismeshTagBoard::begin() {
  NRF52BoardDCDC::begin();

  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(PIN_USER_BTN, INPUT_PULLUP);

  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up

  // Start LEDs with defaults; prefs are applied after loadPrefs()
  static LEDManager _ledManager(LED_BLUE, LED_GREEN);
  ledManager = &_ledManager;
  ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}