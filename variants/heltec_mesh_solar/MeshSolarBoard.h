#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

#ifdef HELTEC_MESH_SOLAR
#include "meshSolarApp.h"
#endif

class MeshSolarBoard : public NRF52BoardOTA {
public:
  MeshSolarBoard() : NRF52BoardOTA("MESH_SOLAR_OTA") {}
  void begin();

  uint16_t getBattMilliVolts() override {
    return meshSolarGetBattVoltage();
  }

  const char* getManufacturerName() const override {
    return "Heltec Mesh Solar";
  }
};
