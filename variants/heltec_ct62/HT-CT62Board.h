#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#if defined(ESP_PLATFORM)

#include <helpers/ESP32Board.h>

class Heltec_CT62_Board : public ESP32Board {
  uint32_t gpio_state = 0;

public:
  void begin() {
    ESP32Board::begin();
#if defined(PIN_BOARD_RELAY_CH1) && defined(PIN_BOARD_RELAY_CH2) 
    pinMode(PIN_BOARD_RELAY_CH1, OUTPUT);
    pinMode(PIN_BOARD_RELAY_CH2, OUTPUT);
#endif
#if defined(PIN_BOARD_DIGITAL_IN)
    pinMode(PIN_BOARD_DIGITAL_IN, INPUT);
#endif
  }
  uint32_t getGpio() override {
#if defined(PIN_BOARD_DIGITAL_IN)
    return gpio_state | (digitalRead(PIN_BOARD_DIGITAL_IN) ? 1 : 0);
#else
    return 0;
#endif
  }
  void setGpio(uint32_t values) override {
#if defined(PIN_BOARD_RELAY_CH1) && defined(PIN_BOARD_RELAY_CH2) 
    gpio_state = values;
    digitalWrite(PIN_BOARD_RELAY_CH1, values & 2);
    digitalWrite(PIN_BOARD_RELAY_CH2, values & 4);
#endif
  }

  uint16_t getBattMilliVolts() override {
  #ifdef PIN_VBAT_READ
    analogReadResolution(12);         // ESP32-C3 ADC is 12-bit - 3.3/4096 (ref voltage/max counts)
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / 8;

    return ((6.52 * raw) / 1024.0) * 1000;
  #else
    return 0;  // not supported
  #endif
  }

  const char* getManufacturerName() const override {
    return "Heltec CT62";
  }
};

#endif
