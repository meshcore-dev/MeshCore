#pragma once

#include <Arduino.h>
#include <MeshCore.h>

// from https://github.com/RAKWireless/RAK11300-AT-Command-Firmware/blob/9c48409a43620a828d653501d536473200aa33af/RAK11300-AT-Arduino/batt.cpp#L17-L19
#define VBAT_MV_PER_LSB (0.806F)   // 3.0V ADC range and 12 - bit ADC resolution = 3300mV / 4096
#define VBAT_DIVIDER (0.6F)		   // 1.5M + 1M voltage divider on VBAT = (1.5M / (1M + 1.5M))
#define VBAT_DIVIDER_COMP (1.846F) //  // Compensation factor for the VBAT divider

#define PIN_VBAT_READ            26
#define BATTERY_SAMPLES          8
#define ADC_MULTIPLIER           (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

class RAK11310Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

#ifdef P_LORA_TX_LED
  void onBeforeTransmit() override { digitalWrite(P_LORA_TX_LED, HIGH); }
  void onAfterTransmit() override { digitalWrite(P_LORA_TX_LED, LOW); }
#endif

  uint16_t getBattMilliVolts() override {
#if defined(PIN_VBAT_READ) && defined(ADC_MULTIPLIER)
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    return (ADC_MULTIPLIER * raw);
#else
    return 0;
#endif
  }

  const char *getManufacturerName() const override { return "RAK 11310"; }

  void reboot() override { rp2040.reboot(); }

  bool startOTAUpdate(const char *id, char reply[]) override;
};
