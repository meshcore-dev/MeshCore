#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include "LoRaFEMControl.h"

class HeltecTrackerV2Board : public ESP32Board {
  KeyValueStore* _prefs = NULL;

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;

  HeltecTrackerV2Board() : periph_power(PIN_VEXT_EN,PIN_VEXT_EN_ACTIVE) { }

  void begin();
  void attachDynamicPrefs(KeyValueStore* prefs);
  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) override;
  bool canControlLoRaFemLna() const override { return loRaFEMControl.isLnaCanControl(); }
  bool setLoRaFemLnaEnabled(bool enable) override;
  bool isLoRaFemLnaEnabled() const override;

  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override ;

};
