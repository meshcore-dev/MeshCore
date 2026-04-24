#ifdef WIO_WM1110

#include "WioWM1110Board.h"

#include <Arduino.h>
#include <Wire.h>

void WioWM1110Board::begin() {
  NRF52BoardDCDC::begin();

  pinMode(BATTERY_PIN, INPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(SENSOR_POWER_PIN, OUTPUT);

  digitalWrite(SENSOR_POWER_PIN, LOW);

  Serial1.begin(115200);

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

  delay(10);

  // Start LEDs with defaults; prefs are applied after loadPrefs()
  static LEDManager _ledManager(LED_GREEN, LED_RED);
  ledManager = &_ledManager;
  ledManager->begin(LED_STATUS_BOOT_30S, LED_ACTIVITY_BOTH);
}
#endif

