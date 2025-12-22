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

  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED, LOW);
  digitalWrite(SENSOR_POWER_PIN, LOW);

  Serial1.begin(115200);

#if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
  Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
#endif

  Wire.begin();

  delay(10);
}
#endif

