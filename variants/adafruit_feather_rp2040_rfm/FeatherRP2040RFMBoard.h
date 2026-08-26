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
 * returns 0 and the node reports 0.00% battery. That is a hardware limit, not
 * a firmware bug.
 *
 * A LiPo can reach 4.2V, so it must be divided before it can be read by the
 * 3.3V ADC. Two 100k 1% resistors from BAT to A0 give a 2:1 divider:
 *
 *      BAT --+ -/\/\/\/\- --+
 *                  100k     |
 *                           +-- A0 (GPIO26)
 *                           |
 *      GND --+ -/\/\/\/\- --+
 *                  100k
 *
 * then add to the env's build_flags:
 *
 *   -D PIN_VBAT_READ=26
 *   -D ADC_MULTIPLIER='(2.0f * 3.3f * 1000)'
 */

#define BATTERY_SAMPLES 8

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
