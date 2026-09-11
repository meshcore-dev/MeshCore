#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

// Lierda AM36 Pico dev board (L-LRMAM36-FANN4-PK02).
// The L-LRMAM36-FANN4 module pairs an ESP32-S3 with a Semtech LR2021.
// Nothing exotic at the board level: USB-C power, no battery divider on the
// dev board. The only LED is a hardwired 3V3 power indicator (no status LED).
class LierdaAM36Board : public ESP32Board {
public:
  LierdaAM36Board() { }

  void begin() {
    ESP32Board::begin();
#ifdef PIN_USER_BTN
    pinMode(PIN_USER_BTN, INPUT_PULLUP);   // KEY, pressed = LOW
#endif
  }

  const char* getManufacturerName() const override {
    return "Lierda AM36 Pico";
  }
};
