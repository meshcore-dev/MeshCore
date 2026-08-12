#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>
#include <helpers/RefCountedDigitalPin.h>

class T1Board : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif
  void variant_shutdown();

public:
  RefCountedDigitalPin periph_power;

  T1Board() : NRF52BoardDCDC("T1_OTA"), periph_power(PIN_TFT_VDD_CTL, PIN_TFT_VDD_CTL_ACTIVE) {}

  void begin();
  void onBeforeTransmit() override;
  void onAfterTransmit() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override;
  void powerOff() override;
};
