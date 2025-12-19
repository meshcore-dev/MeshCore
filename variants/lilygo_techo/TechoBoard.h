#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <Wire.h>
#include <helpers/NRF52Board.h>

// built-ins
#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER      (0.5F)          // 150K + 150K voltage divider on VBAT
#define VBAT_DIVIDER_COMP (2.0F)          // Compensation factor for the VBAT divider

#define PIN_VBAT_READ     (4)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

class TechoBoard : public NRF52BoardOTA {
public:
  TechoBoard() : NRF52BoardOTA("TECHO_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;

  const char* getManufacturerName() const override {
    return "LilyGo T-Echo";
  }

  void powerOff() override {
    #ifdef LED_RED
    digitalWrite(LED_RED, HIGH);
    pinMode(LED_RED, INPUT_PULLUP);
    #endif
    #ifdef LED_GREEN
    digitalWrite(LED_GREEN, HIGH);
    pinMode(LED_GREEN, INPUT_PULLUP);
    #endif
    #ifdef LED_BLUE
    digitalWrite(LED_BLUE, HIGH);
    pinMode(LED_BLUE, INPUT_PULLUP);
    #endif
    #ifdef DISP_BACKLIGHT
    digitalWrite(DISP_BACKLIGHT, LOW);
    pinMode(DISP_BACKLIGHT, INPUT_PULLDOWN);
    #endif
    #ifdef GPS_EN
    digitalWrite(GPS_EN, LOW);
    pinMode(GPS_EN, INPUT_PULLDOWN);
    #endif
    #ifdef PIN_GPS_RESET
    digitalWrite(PIN_GPS_RESET, LOW);
    pinMode(PIN_GPS_RESET, INPUT_PULLDOWN);
    #endif
    Wire.end();
    #ifdef PIN_WIRE_SDA
    pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
    #endif
    #ifdef PIN_WIRE_SCL
    pinMode(PIN_WIRE_SCL, INPUT_PULLUP);
    #endif
    #ifdef SX126X_POWER_EN
    digitalWrite(SX126X_POWER_EN, LOW);
    pinMode(SX126X_POWER_EN, INPUT);
    #endif
    #ifdef PIN_PWR_EN
    digitalWrite(PIN_PWR_EN, LOW);
    pinMode(PIN_PWR_EN, INPUT);
    #endif
    sd_power_system_off();
  }
};
