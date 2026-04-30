#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>
#include "variant.h"

#if defined(HAS_RGB_LED)
  #include <Adafruit_NeoPixel.h>
#endif

class TechoCardBoard : public NRF52BoardDCDC {
private:
  #if defined(HAS_RGB_LED)
    Adafruit_NeoPixel _pixels = Adafruit_NeoPixel(NUM_NEOPIXELS, PIN_RGB_LED_1, NEO_GRB + NEO_KHZ800);
  #endif

public:
  TechoCardBoard() : NRF52Board("TECHO_CARD_OTA") {}

  void begin();

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    pinMode(PIN_BAT_CTL, OUTPUT);
    pinMode(PIN_VBAT_READ, INPUT);
    digitalWrite(PIN_BAT_CTL, HIGH);

    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    digitalWrite(PIN_BAT_CTL, LOW);

    return (uint16_t)((float)adcvalue * MV_LSB * ADC_MULTIPLIER);
  }

  const char* getManufacturerName() const override {
    return "LilyGo T-Echo Card";
  }

  void powerOff() override {
    sd_power_system_off();
  }

  // GPS power control
  void enableGPS(bool enable);

  // Speaker power control
  void enableSpeaker(bool enable);

  // RGB LEDs — all three to same colour
  void setLED(uint8_t r, uint8_t g, uint8_t b);
  void ledOff();

  // Per-LED status control (0=power, 1=notify, 2=pairing)
  void setStatusLED(uint8_t led_index, uint32_t color);

  // Buzzer
  void buzz(uint16_t freq_hz, uint16_t duration_ms);
};