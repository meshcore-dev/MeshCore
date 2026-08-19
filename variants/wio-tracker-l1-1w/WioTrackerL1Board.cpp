#include <Arduino.h>
#include <Wire.h>

#include "WioTrackerL1Board.h"

void WioTrackerL1Board::begin() {
  NRF52BoardDCDC::begin();
  btn_prev_state = HIGH;

  pinMode(PIN_VBAT_READ, INPUT); // VBAT ADC input
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

  pinMode(SX126X_POWER_EN, OUTPUT);

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  delay(10);   // give sx1262 some time to power up
}

bool WioTrackerL1Board::setLoRaFemPaGainEnabled(bool enable) {
  _is_pa_enabled = enable;
  digitalWrite(SX126X_POWER_EN, enable ? HIGH : LOW);   // enable/disable the 1W PA
  return true;
}
