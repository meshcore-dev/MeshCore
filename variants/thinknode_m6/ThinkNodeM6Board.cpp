#include "ThinkNodeM6Board.h"
#include <Arduino.h>

#ifdef THINKNODE_M6

#include <Wire.h>

// A reset in the middle of an RTC read can leave the RTC holding SDA low,
// and the RTC probe at boot then hangs forever. Up to nine clock pulses let
// it finish its byte and release the bus.
static void recoverI2cBus() {
  pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
  pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
  MESH_DEBUG_PRINTLN("I2C at boot: SDA=%d SCL=%d", digitalRead(PIN_WIRE_SDA), digitalRead(PIN_WIRE_SCL));
  pinMode(PIN_WIRE_SCL, OUTPUT);
  digitalWrite(PIN_WIRE_SCL, HIGH);
  for (int i = 0; i < 9 && digitalRead(PIN_WIRE_SDA) == LOW; i++) {
    digitalWrite(PIN_WIRE_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_WIRE_SCL, HIGH);
    delayMicroseconds(5);
  }
  pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
}

void ThinkNodeM6Board::begin() {
  NRF52Board::begin();

  recoverI2cBus();
  Wire.begin();

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);
#endif

  pinMode(EXT_PWR_DETECT, INPUT);
  pinMode(EXT_CHRG_DETECT, INPUT);

  delay(10); // give sx1262 some time to power up
}

uint16_t ThinkNodeM6Board::getBattMilliVolts() {
  int adcvalue = 0;

  digitalWrite(PIN_ADC_CTRL, HIGH);
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(10);

  // ADC range is 0..3000mV and resolution is 12-bit (0..4095)
  adcvalue = analogRead(PIN_VBAT_READ);
  digitalWrite(PIN_ADC_CTRL, LOW);
  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  return (uint16_t)((float)adcvalue * REAL_VBAT_MV_PER_LSB);
}
#endif
