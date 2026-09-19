#include <Arduino.h>
#include <Wire.h>

#include "TechoBoard.h"

#ifdef LILYGO_TECHO

// A soft reset (DFU, watchdog, reset button) does not power-cycle the I2C
// slaves, so a slave caught mid-transfer can still be holding SDA low. The
// nRF52 TWIM driver then busy-waits forever on the first transfer and the
// firmware looks dead before it ever reaches radio_init(). Clock the bus by
// hand until SDA is released, then issue a STOP, exactly as the I2C spec's
// bus-clear procedure describes. Cheap, and a no-op on a healthy bus.
static void i2cBusRecover() {
  pinMode(PIN_WIRE_SCL, OUTPUT_S0D1);   // open-drain with pull-up
  pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
  digitalWrite(PIN_WIRE_SCL, HIGH);
  delayMicroseconds(10);
  for (int i = 0; i < 18 && digitalRead(PIN_WIRE_SDA) == LOW; i++) {
    digitalWrite(PIN_WIRE_SCL, LOW);
    delayMicroseconds(10);
    digitalWrite(PIN_WIRE_SCL, HIGH);
    delayMicroseconds(10);
  }
  // STOP: SDA low -> high while SCL is high
  pinMode(PIN_WIRE_SDA, OUTPUT_S0D1);
  digitalWrite(PIN_WIRE_SDA, LOW);
  delayMicroseconds(10);
  digitalWrite(PIN_WIRE_SCL, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_WIRE_SDA, HIGH);
  delayMicroseconds(10);
  pinMode(PIN_WIRE_SDA, INPUT);
  pinMode(PIN_WIRE_SCL, INPUT);
}

void TechoBoard::begin() {
  NRF52Board::begin();

  i2cBusRecover();
  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up
}

uint16_t TechoBoard::getBattMilliVolts() {
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
