#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class WaveshareS3LR1121Board : public ESP32Board {
public:
  const char* getManufacturerName() const override {
    return "Waveshare ESP32-S3-LR1121";
  }
};
