/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "I2cBusRecovery.h"

#include <Arduino.h>
#include <MeshCore.h>

namespace inhero {

void recoverI2cBus(uint8_t sda, uint8_t scl) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, OUTPUT);
  digitalWrite(scl, HIGH);

  if (digitalRead(sda) == LOW) {
    for (int i = 0; i < 9; i++) {
      digitalWrite(scl, LOW);
      delayMicroseconds(5);
      digitalWrite(scl, HIGH);
      delayMicroseconds(5);
      if (digitalRead(sda) == HIGH) break;
    }
    // STOP condition: SDA LOW->HIGH while SCL is HIGH
    pinMode(sda, OUTPUT);
    digitalWrite(sda, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
    digitalWrite(sda, HIGH);
    delayMicroseconds(5);
    MESH_DEBUG_PRINTLN("I2C bus recovery performed (SDA was stuck LOW)");
  }

  pinMode(sda, INPUT);
  pinMode(scl, INPUT);
}

} // namespace inhero
