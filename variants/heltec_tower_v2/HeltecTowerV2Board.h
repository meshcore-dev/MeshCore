#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>
#include "LoRaFEMControl.h"

class HeltecTowerV2Board : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  LoRaFEMControl loRaFEMControl;

  HeltecTowerV2Board() : NRF52Board("TOWER_V2_OTA") {}
  void begin();
  void onBeforeTransmit() override;
  void onAfterTransmit() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override;
  void powerOff() override;
};
