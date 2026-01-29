#include <Arduino.h>
#include <Wire.h>

#include "MeshPocket.h"

void HeltecMeshPocket::begin() {
  NRF52Board::begin();
  Serial.begin(115200);
  pinMode(PIN_VBAT_READ, INPUT);

  pinMode(PIN_USER_BTN, INPUT);
}
