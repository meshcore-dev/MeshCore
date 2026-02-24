#include <Arduino.h>
#include <Wire.h>

#include "RAK3401Board.h"

void RAK3401Board::begin() {
  NRF52BoardDCDC::begin();
  pinMode(PIN_VBAT_READ, INPUT);
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#ifdef PIN_USER_BTN_ANA
  pinMode(PIN_USER_BTN_ANA, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  pinMode(PIN_3V3_EN, OUTPUT);
  digitalWrite(PIN_3V3_EN, HIGH);

  // Initialize SKY66122-11 FEM on the RAK13302 module.
  // CSD (P0.24) and CPS (P0.21) must be HIGH for both TX and RX modes.
  // CTX (P0.31) selects TX(HIGH) vs RX(LOW) and also enables the 5V boost
  // converter that powers the PA section (VCC1/VCC2).
  // The LNA section (VSUP1/VCC0) runs on 3.3V and works with boost off.
  pinMode(P_LORA_PA_CSD, OUTPUT);
  digitalWrite(P_LORA_PA_CSD, HIGH);       // CSD=1: enable FEM

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);     // CPS=1: enable TX/RX paths

  pinMode(P_LORA_PA_EN, OUTPUT);
  digitalWrite(P_LORA_PA_EN, LOW);         // CTX=0: RX mode, boost off

  delay(1);  // SKY66122 turn-on settling time
}

#ifdef NRF52_POWER_MANAGEMENT
void RAK3401Board::initiateShutdown(uint8_t reason) {
  // Put SKY66122 in guaranteed <1 uA shutdown (Mode 4: CSD=0, CTX=0, CPS=0)
  digitalWrite(P_LORA_PA_EN, LOW);         // CTX=0, boost off
  digitalWrite(SX126X_POWER_EN, LOW);      // CPS=0
  digitalWrite(P_LORA_PA_CSD, LOW);        // CSD=0

  // Disable 3V3 switched peripherals
  digitalWrite(PIN_3V3_EN, LOW);

  enterSystemOff(reason);
}
#endif
