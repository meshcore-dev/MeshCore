#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#ifdef WIO_WM1110

#ifdef Serial
  #undef Serial
#endif
#define Serial Serial1

class WioWM1110Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(LED_GREEN)
  void onBeforeTransmit() override {
    digitalWrite(LED_RED, HIGH);
  }
  void onAfterTransmit() override {
    digitalWrite(LED_RED, LOW);
  }
#endif

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    adcvalue = analogRead(BATTERY_PIN);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE * 1000.0) / 4096.0;
  }

  const char* getManufacturerName() const override {
    return "Seeed Wio WM1110";
  }

  void reboot() override {
    NVIC_SystemReset();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;

  void enableSensorPower(bool enable) {
    digitalWrite(SENSOR_POWER_PIN, enable ? HIGH : LOW);
    if (enable) {
      delay(100);
    }
  }
};

#endif

