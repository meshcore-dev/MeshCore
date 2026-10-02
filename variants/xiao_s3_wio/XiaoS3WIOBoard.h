#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

// built-ins
#ifndef PIN_VBAT_READ              // set in platformio.ini for boards with pulled A0 to VBAT and GND (220kohm)
  #define  PIN_VBAT_READ    1
#endif

class XiaoS3WIOBoard : public ESP32Board {
public:
  XiaoS3WIOBoard() { }

  const char* getManufacturerName() const override {
    return "Xiao S3 WIO";
  }
};
