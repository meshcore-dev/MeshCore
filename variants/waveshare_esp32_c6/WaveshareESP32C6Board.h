#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class WaveshareESP32C6Board : public ESP32Board {
public:
  const char* getManufacturerName() const override {
    return "Waveshare ESP32-C6";
  }
};
