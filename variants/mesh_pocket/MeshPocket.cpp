#include <Arduino.h>
#include <Wire.h>

#include "MeshPocket.h"

void HeltecMeshPocket::begin() {
  NRF52Board::begin();
  Serial.begin(115200);
  pinMode(PIN_VBAT_READ, INPUT);

  pinMode(PIN_USER_BTN, INPUT);

  // Start LEDs with defaults; prefs are applied after loadPrefs()
  static LEDManager _ledManager(PIN_LED, -1, false);
  ledManager = &_ledManager;
  ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}
