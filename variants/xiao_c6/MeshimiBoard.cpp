// Build only for Meshimi board-enabled targets
#ifdef USE_MESHIMI_BOARD
#include "MeshimiBoard.h"

#include <Arduino.h>
#include <Wire.h>

void MeshimiBoard::begin() {
  XiaoC6Board::begin();
  Wire.setClock(400000);
  power.begin(*this, Wire);
}

uint16_t MeshimiBoard::getBattMilliVolts() {
  return power.getVoltageMv();
}

float MeshimiBoard::getBattTemperatureC() {
  return power.getBattTemperatureC();
}

#endif // USE_MESHIMI_BOARD
