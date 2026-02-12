#include <Arduino.h>
#include <Wire.h>

#include "WioTrackerL1Board.h"

void WioTrackerL1Board::begin() {
  NRF52BoardDCDC::begin();
  btn_prev_state = HIGH;

  // Configure battery voltage ADC
  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, HIGH);        // Disable VBAT divider to save power
  analogReadResolution(12);
  analogReference(AR_INTERNAL);
  // Set all button pins to INPUT_PULLUP
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_BUTTON4, INPUT_PULLUP);
  pinMode(PIN_BUTTON5, INPUT_PULLUP);
  pinMode(PIN_BUTTON6, INPUT_PULLUP);
  

  #if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
    Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
  #endif

  Wire.begin();

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  delay(10);   // give sx1262 some time to power up
}

uint16_t WioTrackerL1Board::getBattMilliVolts() {
  digitalWrite(VBAT_ENABLE, LOW);         // Enable VBAT divider
  delayMicroseconds(100);                 // Allow voltage divider to settle

  // Average multiple samples to reduce noise
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) {
    raw += analogRead(PIN_VBAT_READ);
  }
  raw = raw / 8;

  digitalWrite(VBAT_ENABLE, HIGH);        // Disable divider to save power
  return (raw * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
}
