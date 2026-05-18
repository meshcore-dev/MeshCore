#pragma once

#include <helpers/ESP32Board.h>

class LilygoT3S3SX1276Board : public ESP32Board {
public:
  uint32_t getIRQGpio() override {
    return P_LORA_DIO_0; // default for SX1276
  }
};