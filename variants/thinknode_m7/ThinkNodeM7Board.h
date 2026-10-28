#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include "variant.h"
#include "NullDisplayDriver.h"
#include "MomentaryButton.h"

class ThinkNodeM7Board : public ESP32Board {

public:
  void begin();
  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1);
  void powerOff() override;
  const char* getManufacturerName() const override;
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);
  }
};
