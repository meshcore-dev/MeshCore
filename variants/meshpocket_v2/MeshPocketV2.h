#pragma once

#include <helpers/NRF52Board.h>
#include "PowerBank.h"

class MeshPocketV2 : public NRF52Board {
public:
  MeshPocketV2() : NRF52Board("MeshPocket_V2_OTA") {}
  PowerBank powerBank;
  void begin() override;
  void loop() override { powerBank.poll(); }
  uint16_t getBattMilliVolts() override;
  bool isChargerActive() override { return powerBank.snapshot().hasChargingInput(); }
  bool hasChargerStatus() const override { return true; }
  mesh::BoardUiExtension* getUiExtension() override;
  bool toggleUsbData();
  bool isUsbDataEnabled() const;
  const char* getManufacturerName() const override { return "Heltec MeshPocket V2"; }
  void shutdownPeripherals() override;

private:
  class UiExtension;
  UiExtension& uiExtension();
  void setUsbDataEnabled(bool enabled);
};
