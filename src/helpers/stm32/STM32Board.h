#pragma once

#include <Arduino.h>
#include <MeshCore.h>

class STM32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  virtual void begin() { startup_reason = BD_STARTUP_NORMAL; }

  uint8_t getStartupReason() const override { return startup_reason; }

  uint16_t getBattMilliVolts() override {
    return 0; // not supported
  }

  const char *getManufacturerName() const override { return "Generic STM32"; }

  void reboot() override { NVIC_SystemReset(); }

  void powerOff() override {
    HAL_PWREx_DisableInternalWakeUpLine();
    __disable_irq();
    HAL_PWREx_EnterSHUTDOWNMode();
  }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW); // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH); // turn TX LED off
  }
#endif

  bool startOTAUpdate(const char *id, char reply[]) override { return false; };
};