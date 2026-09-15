#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class ThinkNodeM9Board : public ESP32Board {

public:
  void begin();
  void powerOff() override;
  const char* getManufacturerName() const override;
  uint32_t getIRQGpio() override;
};
