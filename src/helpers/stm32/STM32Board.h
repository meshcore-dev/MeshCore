#pragma once

#include <MeshCore.h>
#include <Arduino.h>

class STM32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

  // Performs one-time low-power configuration at boot (the SMPS on STM32WL); it is
  // a no-op on other STM32 parts.
  void lowPowerInit();

public:
  virtual void begin() {
    startup_reason = BD_STARTUP_NORMAL;
    lowPowerInit();
  }

  uint8_t getStartupReason() const override { return startup_reason; }

  uint16_t getBattMilliVolts() override {
    return 0;  // not supported
  }

  const char* getManufacturerName() const override {
    return "Generic STM32";
  }

  void reboot() override {
    NVIC_SystemReset(); 
  }

  void powerOff() override {
    HAL_PWREx_DisableInternalWakeUpLine();
    __disable_irq();
    HAL_PWREx_EnterSHUTDOWNMode();
  }

  // Low-power sleep until a LoRa frame arrives or 'secs' elapse (STM32WL only).
  void sleep(uint32_t secs) override;

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED off
  }
#endif

  bool startOTAUpdate(const char* id, char reply[]) override { return false; };
};