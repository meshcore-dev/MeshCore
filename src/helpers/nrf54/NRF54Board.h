#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/KeyValueStore.h>

class NRF54Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  uint32_t bootloader_version = 0;
  const char* ota_name;

public:
  NRF54Board(const char* otaname) : ota_name(otaname) { }

  virtual void begin();

  void attachDynamicPrefs(KeyValueStore* prefs) { }  // no-op

  uint8_t getStartupReason() const override { return startup_reason; }
  float getMCUTemperature() override;
  void reboot() override { NVIC_SystemReset(); }
  bool getBootloaderVersion(char* version, size_t max_len) override;
  bool startOTAUpdate(const char* id, char reply[]) override;
  void loop() override;  // overrides must call this, or start ota and DFU jumps never reboot
};
