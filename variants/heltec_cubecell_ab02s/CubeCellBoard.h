#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/KeyValueStore.h>

extern "C" void CySoftwareReset(void);

#ifdef ASR650X_STACK_WATCH
// Measurement builds only (never released): DEVICE_QUERY reports stack/heap high-water marks in the
// manufacturer field.
void asr650xStackPaint();
const char *asr650xWatchString();
#endif

class CubeCellBoard : public mesh::MainBoard {
public:
  void begin();                               // crypto stack, OLED status screen
  void loop() override;                       // OLED status screen (one page per call)
  void attachDynamicPrefs(KeyValueStore *) {} // no board-specific prefs
  uint16_t getBattMilliVolts() override { return getBatteryVoltage(); }
  const char *getManufacturerName() const override {
#ifdef ASR650X_STACK_WATCH
    return asr650xWatchString();
#else
    return "Heltec CubeCell HTCC-AB02S";
#endif
  }
  void reboot() override { CySoftwareReset(); }
  uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }
};
