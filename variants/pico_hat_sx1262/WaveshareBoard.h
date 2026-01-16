#pragma once

#include <Arduino.h>
#include <MeshCore.h>

// Waveshare LoRa pico hat pins:
// https://www.waveshare.com/pico-lora-sx1262-868m.htm

class WaveshareBoard : public mesh::MainBoard {
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
#if defined(P_VBAT_READ) && defined(ADC_MULTIPLIER)
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(P_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    return (ADC_MULTIPLIER * raw) / 4096;
#else
    return 0;
#endif
  }

  const char *getManufacturerName() const override { return "Waveshare RP2040-LoRa"; }

  void reboot() override { rp2040.reboot(); }

  bool startOTAUpdate(const char *id, char reply[]) override;
};
