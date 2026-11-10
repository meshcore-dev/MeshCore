#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>

class DFRobotLoRaBoard : public ESP32Board {
public:
  RefCountedDigitalPin periph_power;

  DFRobotLoRaBoard() : periph_power(PIN_VEXT_EN, PIN_VEXT_EN_ACTIVE) { }

  void begin();
  const char* getManufacturerName() const override;
};
