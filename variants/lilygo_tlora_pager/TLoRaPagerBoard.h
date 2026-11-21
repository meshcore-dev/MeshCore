#pragma once

#include <helpers/ESP32Board.h>

class TLoRaPagerBoard : public ESP32Board {
  bool _ready = false;

public:
  void begin();
  bool peripheralsReady() const { return _ready; }
  const char* getManufacturerName() const override { return "LILYGO T-LoRa Pager"; }
  uint16_t getBattMilliVolts() override;
};
