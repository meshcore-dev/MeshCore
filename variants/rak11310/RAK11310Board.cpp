#include "RAK11310Board.h"

#include <Arduino.h>
#include <Wire.h>

void RAK11310Board::begin() {
  // for future use, sub-classes SHOULD call this from their begin()
  startup_reason = BD_STARTUP_NORMAL;

#ifdef PIN_VBAT_READ
  pinMode(PIN_VBAT_READ, INPUT);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setSDA(PIN_BOARD_SDA);
  Wire.setSCL(PIN_BOARD_SCL);
#endif

  Wire.begin();

  delay(10); // give sx1262 some time to power up

  // Start LEDs with defaults; prefs are applied after loadPrefs()
  // green led = 23, blue led = P_LORA_TX_LED (24)
  static LEDManager _ledManager(23, P_LORA_TX_LED);
  ledManager = &_ledManager;
  ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}

bool RAK11310Board::startOTAUpdate(const char *id, char reply[]) {
  return false;
}
