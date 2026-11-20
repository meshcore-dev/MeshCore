#pragma once

#include <helpers/ESP32Board.h>

class Chatter2Board : public ESP32Board {
public:
  uint16_t getBattMilliVolts() override {
    uint32_t raw_millivolts = 0;
    for (int i = 0; i < 4; i++) {
      raw_millivolts += analogReadMilliVolts(PIN_VBAT_READ);
    }

    return static_cast<uint16_t>((raw_millivolts / 4.0f) * ADC_MULTIPLIER);
  }

  const char* getManufacturerName() const override {
    return "CircuitMess Chatter 2";
  }
};
