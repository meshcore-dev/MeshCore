#pragma once

#include <helpers/ESP32Board.h>

class TETHEliteBoard : public ESP32Board {
public:
  const char* getManufacturerName() const override {
    return "LilyGO T-ETH Elite";
  }
};
