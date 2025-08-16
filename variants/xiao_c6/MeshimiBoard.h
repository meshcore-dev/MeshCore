#pragma once

#include <Arduino.h>
#include "XiaoC6Board.h"

// Meshimi-specific board implementation for Xiao ESP32-C6
class MeshimiBoard : public XiaoC6Board {
public:
  void begin();
  uint16_t getBattMilliVolts() override;
  float getBattTemperatureC();

  const char* getManufacturerName() const override {
    return "Meshimi";
  }
};


