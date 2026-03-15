#include <Arduino.h>
#include <Wire.h>

#include "T1000eBoard.h"

void T1000eBoard::begin() {
  NRF52BoardDCDC::begin();
  btn_prev_state = HIGH;

#ifdef BUTTON_PIN
  pinMode(BATTERY_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  delay(10);   // give sx1262 some time to power up
}

bool T1000eBoard::isExternalPowered() {
  // T1000-E intentionally relies on its PMIC status lines rather than the
  // generic nRF52 USB/VBUS detection path.
  bool externalPowerDetected = false;
  bool chargingDetected = false;

#ifdef EXT_PWR_DETECT
  externalPowerDetected = digitalRead(EXT_PWR_DETECT) == HIGH;
#endif

#ifdef EXT_CHRG_DETECT
  chargingDetected = digitalRead(EXT_CHRG_DETECT) == LOW;
#endif

  return externalPowerDetected || chargingDetected;
}
