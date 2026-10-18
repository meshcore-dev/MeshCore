#include <Arduino.h>
#include <Wire.h>

#include "TImpulsePlusBoard.h"

void TImpulsePlusBoard::begin() {
  NRF52BoardDCDC::begin();

  // turn on 3.3v
  pinMode(RT9080_EN, OUTPUT);
  digitalWrite(RT9080_EN, HIGH);
  delay(100);

  // init gps pins
  pinMode(GPS_EN, OUTPUT);
  digitalWrite(GPS_EN, HIGH); // gps off by default

  // configure battery measurement
  pinMode(BATTERY_ADC_DATA, INPUT);
  pinMode(BATTERY_MEASUREMENT_CONTROL, OUTPUT);
  digitalWrite(BATTERY_MEASUREMENT_CONTROL, LOW); // turn off battery voltage measurement

  // set the analog reference to 3.0V (default = 3.6V)
  analogReference(AR_INTERNAL_3_0);
  
  // set the resolution to 12-bit (0..4095)
  analogReadResolution(12); // Can be 8, 10, 12 or 14

  // configure user button
  pinMode(TTP223_KEY, INPUT_PULLDOWN);

  #if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
    Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
  #endif

  Wire.begin();

  delay(10); // give sx1262 some time to power up
}
