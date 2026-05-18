#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// ============================================================
// T-Echo Lite battery pins — hardcoded from LilyGo t_echo_lite_config.h
// NOT using any defines from variant.h for battery measurement
// ============================================================
#define PIN_VBAT_READ         _PINNUM(0, 2)   // BATTERY_ADC_DATA
#define PIN_VBAT_MEAS_EN      _PINNUM(0, 31)  // BATTERY_MEASUREMENT_CONTROL

class TechoBoard : public NRF52BoardDCDC {
public:
  TechoBoard() : NRF52Board("TECHO_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;

  const char* getManufacturerName() const override {
    return "LilyGo T-Echo Lite";
  }

  void powerOff() override {
    digitalWrite(PIN_VBAT_MEAS_EN, LOW);
    #ifdef LED_RED
    digitalWrite(LED_RED, LOW);
    #endif
    #ifdef LED_GREEN
    digitalWrite(LED_GREEN, LOW);
    #endif
    #ifdef LED_BLUE
    digitalWrite(LED_BLUE, LOW);
    #endif
    #ifdef DISP_BACKLIGHT
    digitalWrite(DISP_BACKLIGHT, LOW);
    #endif
    #ifdef PIN_PWR_EN
    digitalWrite(PIN_PWR_EN, LOW);
    #endif
    sd_power_system_off();
  }
};