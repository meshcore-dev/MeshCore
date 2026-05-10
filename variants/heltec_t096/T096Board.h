#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>
#include <helpers/RefCountedDigitalPin.h>
#include "LoRaFEMControl.h"

class T096Board : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif
  void variant_shutdown();

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;

  T096Board() :periph_power(PIN_VEXT_EN,PIN_VEXT_EN_ACTIVE), NRF52Board("T096_OTA") {}
  void begin();

  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override ;
  void powerOff() override;
};
