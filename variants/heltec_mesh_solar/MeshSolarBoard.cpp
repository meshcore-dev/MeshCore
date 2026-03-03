#include <Arduino.h>
#include <Wire.h>

#include "MeshSolarBoard.h"

void MeshSolarBoard::begin() {
  NRF52Board::begin();
  pinMode(30, OUTPUT);
  pinMode(5, OUTPUT);
  meshSolarStart();

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();
}
