#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/ESP32Board.h>
#include "pins_arduino.h"

#define P_LORA_TX_LED LED_BUILTIN // defined in pins_arduino.h / expander.cpp through pin handling functions specific to the IO expander
// #define PIN_TFT_RST LCD_RESET 
// #define PIN_TFT_LEDA_CTL LCD_BACKLIGHT

class ArduinoNessoN1Board : public ESP32Board {
private:
  NessoBattery battery;

public:
  void begin(); // Defined in ArduinoNessoN1Board.cpp

#ifdef P_LORA_TX_LED
  void onBeforeTransmit() override {
    MESH_DEBUG_PRINTLN("onBeforeTransmit: LOW LED for On");
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED on
  }
  void onAfterTransmit() override {
    MESH_DEBUG_PRINTLN("onBeforeTransmit: HIGH LED for Off");
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED off
  }
#endif

  const char* getManufacturerName() const override {
    return "Arduino Nesso N1";
  }

  uint16_t getBattMilliVolts() override {
    return battery.getMilliVoltage();
  }

  void reboot() override {
    MESH_DEBUG_PRINTLN("ArduinoNessoN1.reboot(): noop() instead");
    // esp_restart();
  }
};


