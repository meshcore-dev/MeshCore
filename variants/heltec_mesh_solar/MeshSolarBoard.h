#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

#ifdef HELTEC_MESH_SOLAR
#include "meshSolarApp.h"
#endif

// LoRa radio module pins for Heltec T114
#define  P_LORA_DIO_1     20
#define  P_LORA_NSS       24
#define  P_LORA_RESET     25
#define  P_LORA_BUSY      17
#define  P_LORA_SCLK      19
#define  P_LORA_MISO      23
#define  P_LORA_MOSI      22

#define SX126X_DIO2_AS_RF_SWITCH  true
#define SX126X_DIO3_TCXO_VOLTAGE   1.8

class MeshSolarBoard : public NRF52BoardDCDC {
public:
  MeshSolarBoard() : NRF52Board("MESH_SOLAR_OTA") {}
  void begin();

  uint16_t getBattMilliVolts() override {
    return meshSolarGetBattVoltage();
  }

  const char* getManufacturerName() const override {
    return "Heltec Mesh Solar";
  }
};
