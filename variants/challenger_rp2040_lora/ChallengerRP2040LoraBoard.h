#pragma once

#include <Arduino.h>
#include <MeshCore.h>

#define PIN_LED 24

class ChallengerRP2040LoraBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }
  void onBeforeTransmit() override {
    digitalWrite(PIN_LED, HIGH); // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(PIN_LED, LOW); // turn TX LED off
  }
  const char *getManufacturerName() const override { return "Challenger RP2040 Lora"; }
  void reboot() override { rp2040.reboot(); }

  // Unsupported functions
  bool startOTAUpdate(const char *id, char reply[]) override { return false; };
  uint16_t getBattMilliVolts() override { return 0; };
};
