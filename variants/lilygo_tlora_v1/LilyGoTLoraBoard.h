#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

// LILYGO T-LoRa V1 board with SX1276
class LilyGoTLoraBoard : public ESP32Board {
public:
  const char* getManufacturerName() const override {
    return "LILYGO T-LoRa V1";
  }

  // LILYGO T-LoRa V1 has no battery monitoring ADC

};