#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class XiaoS3Board : public ESP32Board {
public:
  XiaoS3Board() { }

  const char* getManufacturerName() const override {
    return "Xiao S3";
  }
};
