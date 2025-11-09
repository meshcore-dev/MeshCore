#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

class XiaoS3WIOBoard : public ESP32Board {
public:
  XiaoS3WIOBoard() { }

  const char* getManufacturerName() const override {
    return "Xiao S3 WIO";
  }
};
