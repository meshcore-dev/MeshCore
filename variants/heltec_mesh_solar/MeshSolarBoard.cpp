#include "MeshSolarBoard.h"

#include <Arduino.h>
#include <Wire.h>

void MeshSolarBoard::begin() {
  NRF52Board::begin();

  meshSolarStart();

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();
}
