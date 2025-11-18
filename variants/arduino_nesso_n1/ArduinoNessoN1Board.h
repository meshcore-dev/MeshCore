#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>
#include "pins_arduino.h"

#define P_LORA_TX_LED LED_BUILTIN // ToDo: doesn't appear to blink on receipt



class ArduinoNessoN1Board : public ESP32Board {
private:
  NessoBattery battery;
public:
  void begin() {
    ESP32Board::begin();

#ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
#endif
  }

#ifdef P_LORA_TX_LED
  void onBeforeTransmit() override {
    MESH_DEBUG_PRINTLN("onBeforeTransmit: HIGH LED");
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    MESH_DEBUG_PRINTLN("onBeforeTransmit: LOW LED");
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  const char* getManufacturerName() const override {
    return "Arduino Nesso N1";
  }

  uint16_t getBattMilliVolts() override {
    return battery.getMilliVoltage(); // ToDo: Needs work - full battery reports 65v 😅
  }
};


