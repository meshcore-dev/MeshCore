#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

#ifdef HELTEC_MESH_SOLAR
#include "meshSolarApp.h"
#endif

class MeshSolarBoard : public NRF52Board {
protected:
  uint8_t startup_reason;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

  uint16_t getBattMilliVolts() override {
    return meshSolarGetBattVoltage();
  }

  const char* getManufacturerName() const override {
    return "Heltec Mesh Solar";
  }

  void reboot() override {
    NVIC_SystemReset();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
};
