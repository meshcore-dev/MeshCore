#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/KeyValueStore.h>

// LoRa radio module pins for the Waveshare Pico-LoRa-SX1262 HAT on a Pico 2 (RP2350)
// https://www.waveshare.com/wiki/Pico-LoRa-SX1262

// The HAT exposes its battery voltage divider on BAT_AD (GP26).
// TODO: verify the divider ratio against the HAT schematic; the multiplier below
// is a placeholder borrowed from the Waveshare RP2040-LoRa board.
#define PIN_VBAT_READ            26
#define BATTERY_SAMPLES          8
#define ADC_MULTIPLIER           (3.0f * 3.3f * 1000)

class PicoLoRaBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

  void attachDynamicPrefs(KeyValueStore* prefs) { }  // no-op

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

    return (ADC_MULTIPLIER * raw) / 4096;
#else
    return 0;
#endif
  }

  // RP2350 internal temperature sensor (arduino-pico core helper, datasheet formula)
  float getMCUTemperature() override { return analogReadTemp(); }

  const char *getManufacturerName() const override { return "Waveshare Pico-LoRa-SX1262 (Pico 2)"; }

  void reboot() override { rp2040.reboot(); }

  bool startOTAUpdate(const char *id, char reply[]) override;
};
