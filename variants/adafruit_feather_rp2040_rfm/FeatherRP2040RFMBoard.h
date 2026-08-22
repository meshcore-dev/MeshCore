#pragma once

#include <Arduino.h>
#include <MeshCore.h>

/*
 * Adafruit Feather RP2040 with RFM95 LoRa Radio (product 5714)
 *   https://learn.adafruit.com/feather-rp2040-rfm95
 *
 * The RFM95 (SX1276) is hard-wired to the RP2040's SPI1 block, which the
 * Arduino core exposes as the default `SPI` object on this board.
 *
 * NOTE: this board has no battery voltage divider, so getBattMilliVolts()
 * returns 0. Wiring a divider from VBAT to one of A0..A3 and defining
 * PIN_VBAT_READ / ADC_MULTIPLIER in the build flags will enable it.
 */

class FeatherRP2040RFMBoard : public mesh::MainBoard {
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

    return (ADC_MULTIPLIER * raw) / 4096;
#else
    return 0;
#endif
  }

  const char* getManufacturerName() const override { return "Adafruit Feather RP2040 RFM"; }

  void reboot() override { rp2040.reboot(); }

  bool startOTAUpdate(const char* id, char reply[]) override;
};
