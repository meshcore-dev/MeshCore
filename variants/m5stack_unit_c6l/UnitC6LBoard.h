#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class UnitC6LBoard : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();
  }

  const char* getManufacturerName() const override {
    return "Unit C6L";
  }
};
