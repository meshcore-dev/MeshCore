#pragma once

#include <MeshCore.h>
#include <Arduino.h>

// Minimal MainBoard for the Seeed XIAO nRF54L15 on the lolren bare-metal Arduino
// core. Implements only the mesh::MainBoard pure virtuals plus begin(); battery,
// power management, sleep, OTA etc. are stubbed for bring-up (Phase 4) and filled
// in later phases against the core's APIs.

#ifndef PIN_VBAT_READ
  #define PIN_VBAT_READ  (PIN_A7)   // XIAO nRF54L15: AIN7 / VBAT divider (see core pins_arduino.h)
#endif

class XiaoNrf54l15Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason = 0;

public:
  void begin() {
    startup_reason = 0;  // BOOT_REASON normal; refine with hwinfo/reset-cause later
  }

  uint16_t getBattMilliVolts() override {
    // TODO Phase 5: read PIN_VBAT_READ via ADC + the board's divider ratio.
    return 0;
  }

  const char* getManufacturerName() const override {
    return "Seeed XIAO nRF54L15";
  }

  void reboot() override {
    NVIC_SystemReset();   // Cortex-M33 system reset
  }

  uint8_t getStartupReason() const override { return startup_reason; }
};
