#include <Arduino.h>
#include "nano-g2.h"

#ifdef NANO_G2_ULTRA

#include <Wire.h>

void NanoG2Ultra::begin()
{
  NRF52Board::begin();

  // set user button
  pinMode(PIN_BUTTON1, INPUT);

  // the external notification circuit is shared for both buzzer and led
  pinMode(EXT_NOTIFY_OUT, OUTPUT);
  digitalWrite(EXT_NOTIFY_OUT, LOW);

  pinMode(GPS_EN, OUTPUT); // Initialize GPS power pin
  
  Wire.begin();
  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);

  delay(10);
}

uint16_t NanoG2Ultra::getBattMilliVolts()
{
  int adcvalue = 0;

  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(10);

  // ADC range is 0..3000mV and resolution is 12-bit (0..4095)
  adcvalue = analogRead(PIN_VBAT_READ);
  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  return (uint16_t)((float)adcvalue * REAL_VBAT_MV_PER_LSB);
}
#endif
