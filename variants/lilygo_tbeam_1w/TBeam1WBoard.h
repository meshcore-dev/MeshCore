#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <helpers/ESP32Board.h>
#include "variant.h"

// LilyGo T-Beam 1W with SX1262 + external PA (XY16P35 module)
//
// Power architecture (LDO is separate chip on T-Beam board, not inside XY16P35):
//
//   VCC (+4.0~+8.0V) ──┬──────────────────► XY16P35 VCC pin 5 (PA direct)
//   (USB or Battery)   │
//                      │   ┌───────────┐
//                      └──►│ LDO Chip  │──► +3.3V ──► XY16P35 (SX1262 + LNA)
//                          │ EN=GPIO40 │
//                          └───────────┘
//                      LDO_EN (GPIO 40): H @ +1.2V~VIN, active high, not floating
//
// Control signals:
//   - LDO_EN (GPIO 40): HIGH enables LDO → powers SX1262 + LNA
//   - TCXO_EN (DIO3):   HIGH enables TCXO (set to 1.8V per Meshtastic)
//   - CTL (GPIO 21):    HIGH=RX (LNA on), LOW=TX (LNA off)
//   - DIO2:             AUTO via SX126X_DIO2_AS_RF_SWITCH (TX path)
//
// Power notes:
//   - PA needs VCC 4.0-8.0V for full 32dBm output
//   - USB-C (3.9-6V) marginal; 7.4V battery recommended
//   - Battery must support 2A+ discharge for high-power TX

class TBeam1WBoard : public ESP32Board {
public:
  enum FanMode { FAN_ON, FAN_OFF, FAN_AUTO };

private:
  bool radio_powered = false;
  bool _stopped = false;
  KeyValueStore* _prefs = nullptr;
  FanMode _mode = FAN_AUTO;
  int _lo_c = FAN_DEFAULT_LO_C;
  int _hi_c = FAN_DEFAULT_HI_C;
  bool _thermal_on = false;  // onoff hysteresis; TX boost must not latch this
  bool _fan_on = true;
  float _temp_c = NAN;
  bool _tx_active = false;
  bool _tx_cooldown_active = false;
  uint32_t _tx_until_ms = 0;
  TaskHandle_t _fan_task = nullptr;
  mutable portMUX_TYPE _fan_mux = portMUX_INITIALIZER_UNLOCKED;

  void startFanTask();
  void updateFan();
  void setFanOutputLocked(bool enabled);
  float readNtcTempC();
  int cooldownSecsLocked();
  bool isTxCoolingLocked(uint32_t now);
  bool ntcImplausible(float temp_c) const;
  bool persistKey(const char* key, const char* value);
  static bool parseIntArg(const char* text, int& value);
  void loadFanPrefs();
  const char* modeNameLocked() const;
  static void fanTaskThunk(void* arg);

public:
  void begin();
  void attachDynamicPrefs(KeyValueStore* prefs);
  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) override;
  void onBeforeTransmit() override;
  void onAfterTransmit() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override;
  void powerOff() override;

  void setFanEnabled(bool enabled);
  bool isFanEnabled() const;
};
