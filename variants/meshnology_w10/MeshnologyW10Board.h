#pragma once

#include <helpers/ESP32Board.h>
#include <XPowersLib.h>
#include "MCP23017.h"

class MeshnologyW10Board : public ESP32Board {
  XPowersAXP2101 _pmu;
  bool _pmu_ready = false;
  bool _ready = false;

public:
  static constexpr uint8_t PIN_SD_CS = 9;
  static constexpr uint8_t EXIO_LCD_RESET = 1;
  static_assert(isExpanderVirtualPin(P_LORA_RESET) && isExpanderVirtualPin(P_LORA_DIO_1) && isExpanderVirtualPin(P_LORA_BUSY),
                "SX1262 RESET, DIO1 and BUSY are MCP23017 lines and must be given as virtual pins");
  static constexpr uint8_t EXIO_LORA_RESET = expanderPin(P_LORA_RESET);
  static constexpr uint8_t EXIO_LORA_DIO1 = expanderPin(P_LORA_DIO_1);
  static constexpr uint8_t EXIO_LORA_BUSY = expanderPin(P_LORA_BUSY);
  static constexpr uint8_t EXIO_AMP_ENABLE = 7;
  static constexpr uint8_t EXIO_GPS_WAKE = 12;

  MCP23017 io;

  MeshnologyW10Board() : io(Wire) { }
  void begin();
  bool isReady() const { return _ready; }
  uint16_t getBattMilliVolts() override;
  bool isExternalPowered() override { return _pmu_ready && _pmu.isVbusIn(); }
  const char* getManufacturerName() const override { return "Meshnology W10"; }
  uint32_t getIRQGpio() override { return (uint32_t)-1; }
  void sleep(uint32_t secs) override { delay(1); } // Radio DIO1 can't wake MCU so no auto light sleep
  void powerOff() override;
};
