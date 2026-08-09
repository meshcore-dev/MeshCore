/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "SystemSleepGpio.h"

#include <Arduino.h>
#include <MeshCore.h>
#include <nrf.h>

#include "../InheroMr2Board.h"
#include "../target.h"

namespace inhero {

void prepareRadioForSystemOff(bool radioInitialized) {
  if (radioInitialized) {
    // Cold sleep via SPI while SPIM is still active.
    radio_driver.powerOff();
    delay(10);
  } else {
    // Early Boot: SPI/RadioLib not initialised. SX1262 may be in POR Standby RC
    // (~600uA) or Cold Sleep (~160nA). Send SetSleep via bit-banged SPI to be sure.
    pinMode(P_LORA_NSS, OUTPUT);
    digitalWrite(P_LORA_NSS, HIGH);
    pinMode(P_LORA_SCLK, OUTPUT);
    digitalWrite(P_LORA_SCLK, LOW);   // CPOL=0
    pinMode(P_LORA_MOSI, OUTPUT);
    digitalWrite(P_LORA_MOSI, LOW);
    pinMode(P_LORA_BUSY, INPUT);

    // SX1262 §13.1.1: a wake-up NSS pulse is consumed; the actual command
    // needs a SUBSEQUENT NSS falling edge.
    digitalWrite(P_LORA_NSS, LOW);
    delayMicroseconds(2);
    uint32_t t0 = millis();
    while (digitalRead(P_LORA_BUSY) == HIGH && (millis() - t0) < 10) {
      delayMicroseconds(100);
    }
    digitalWrite(P_LORA_NSS, HIGH);
    delayMicroseconds(10);

    // SetSleep 0x84 0x00 (Cold Start, no retention, TCXO off).
    static const uint8_t cmd[2] = { 0x84, 0x00 };

    digitalWrite(P_LORA_NSS, LOW);
    delayMicroseconds(2);

    for (int b = 0; b < 2; b++) {
      uint8_t byte = cmd[b];
      for (int i = 7; i >= 0; i--) {
        digitalWrite(P_LORA_MOSI, (byte >> i) & 1);
        delayMicroseconds(1);
        digitalWrite(P_LORA_SCLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(P_LORA_SCLK, LOW);
        delayMicroseconds(1);
      }
    }

    digitalWrite(P_LORA_MOSI, LOW);
    delayMicroseconds(1);
    digitalWrite(P_LORA_NSS, HIGH);

    delay(1);
  }

  // PE4259 RF switch off
  digitalWrite(SX126X_POWER_EN, LOW);

  // Latch SX1262 SPI pins at defined levels so floating CMOS inputs don't
  // pull shoot-through current during System Sleep.
  uint32_t pin_cfg_out = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
                          (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
                          (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) |
                          (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
                          (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
  NRF_P1->OUTSET = (1UL << 10);                        // NSS  HIGH
  NRF_P1->OUTCLR = (1UL << 11) | (1UL << 12);         // SCLK LOW, MOSI LOW
  NRF_P1->PIN_CNF[10] = pin_cfg_out;
  NRF_P1->PIN_CNF[11] = pin_cfg_out;
  NRF_P1->PIN_CNF[12] = pin_cfg_out;
}

void disconnectLeakyPullups() {
  uint32_t pin_cfg_discon = (GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) |
                            (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
                            (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) |
                            (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
                            (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);

  // P0: skip BQ_CE_PIN (P0.04) and RTC_INT_PIN (P0.17)
  for (uint8_t pin = 0; pin < 32; pin++) {
    if (pin == 4 || pin == 17) continue;
    NRF_P0->PIN_CNF[pin] = pin_cfg_discon;
  }
  // P1: skip SX1262 SPI pins latched by prepareRadioForSystemOff()
  for (uint8_t pin = 0; pin < 16; pin++) {
    if (pin == 10 || pin == 11 || pin == 12) continue;
    NRF_P1->PIN_CNF[pin] = pin_cfg_discon;
  }
}

} // namespace inhero
