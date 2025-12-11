#pragma once

#include <MeshCore.h>
#include <Arduino.h>

// built-ins
#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER      (0.5F)          // 150K + 150K voltage divider on VBAT
#define VBAT_DIVIDER_COMP (2.0F)          // Compensation factor for the VBAT divider

#define PIN_VBAT_READ     (2)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

#define BATTERY_MEASUREMENT_CONTROL _PINNUM(0, 31)
#define BATTERY_SAMPLES   (8)

class TechoBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:

  void begin();
  uint16_t getBattMilliVolts() override;
  bool startOTAUpdate(const char* id, char reply[]) override;

  uint8_t getStartupReason() const override {
    return startup_reason;
  }

  const char* getManufacturerName() const override {
    return "LilyGo T-Echo";
  }

  void powerOff() override {
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

  void reboot() override {
    NVIC_SystemReset();
  }
};
