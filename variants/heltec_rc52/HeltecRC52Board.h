#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>
#include <helpers/RefCountedDigitalPin.h>

class HeltecRC52Board : public NRF52BoardDCDC {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  RefCountedDigitalPin periph_power;

  HeltecRC52Board() : NRF52Board("RC52_OTA"), periph_power(SENSOR_POWER_CTRL_PIN, SENSOR_POWER_ON) {}

  void begin() override;
  void onBeforeTransmit() override;
  void onAfterTransmit() override;
  void shutdownPeripherals() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override;
};
