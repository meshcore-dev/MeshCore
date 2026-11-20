#pragma once

#include <helpers/NRF52Board.h>
#include "PowerBank.h"
#include "PowerBankUi.h"

class HeltecMeshPocketV2Board : public NRF52Board {
public:
  HeltecMeshPocketV2Board() : NRF52Board("MeshPocket_V2_OTA") {}
  PowerBank powerBank;
  PowerBankUi powerUi{*this};
  void begin() override;
  void loop() override { powerBank.poll(); }
  uint16_t getBattMilliVolts() override;
  bool isChargerActive() override { return powerBank.snapshot().hasChargingInput(); }
  bool toggleUsbData();
  bool isUsbDataEnabled() const;
  const char* getManufacturerName() const override { return "Heltec MeshPocket V2"; }
  void shutdownPeripherals() override;

private:
  void setUsbDataEnabled(bool enabled);
};
