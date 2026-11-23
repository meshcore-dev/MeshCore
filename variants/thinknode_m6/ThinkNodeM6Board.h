#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

// built-ins
#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER_COMP ADC_MULTIPLIER          // Compensation factor for the VBAT divider

#define PIN_VBAT_READ     BATTERY_PIN
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

#define M6_POWEROFF_HOLD_MILLIS     3900    // Meshtastic: hold time before power off
#define M6_BTN_BOOT_HOLDOFF_MILLIS  30000   // Meshtastic: ignore presses that start earlier
#define M6_BATT_READ_MILLIS         30000
#define M6_WATCHDOG_SECS            90
#define M6_DFU_MAGIC_SKIP           0x6D    // tells the bootloader to start the firmware at once
#define M6_RX_FLASH_MILLIS          400     // blue flashing after each received packet
#define M6_POWERON_HOLD_MILLIS      3000    // Elecrow: hold 3 s to power on
#define M6_POWEROFF_MARK            0x4D    // in GPREGRET2: the board was powered off by powerOff()

class ThinkNodeM6Board : public NRF52BoardDCDC {
  bool _red_led_on = true;
  bool _blue_led_on = false;
  bool _tx_active = false;
  uint32_t _data_started_at = 0;
  uint32_t _rx_count = 0;
  uint32_t _rx_at = 0;
  uint32_t _batt_read_at = 0;
  uint16_t _batt_mv = 0;
  uint32_t _btn_down_at = 0;

  void checkPowerOnPress();

protected:
#if NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  ThinkNodeM6Board() : NRF52Board("THINKNODE_M6_OTA") {}
  void begin();
  void loop() override;
  uint16_t getBattMilliVolts() override;

  // loop() flashes the blue LED while _tx_active is set
  void onBeforeTransmit() override {
    _tx_active = true;
    _data_started_at = millis();
  }
  void onAfterTransmit() override {
    _tx_active = false;
  }

  const char* getManufacturerName() const override {
    return "Elecrow ThinkNode M6";
  }

  bool hasUsbPowerDetect() const override { return true; }

  bool isUsbPowered() override {
    // P0.13 is the dedicated USB / external power-detect line on the M6 PCB
    // (same pin the Meshtastic M6 variant uses for USB detection).
    return digitalRead(EXT_PWR_DETECT) == EXT_PWR_DETECT_VALUE;
  }

  bool hasSolarChargerDetect() const override { return true; }

  bool isSolarChargerActive() override {
    // P0.15 is the active-low solar-charger status line. It reports the
    // charger's state, not photovoltaic voltage or current.
    return digitalRead(EXT_CHRG_DETECT) == EXT_CHRG_DETECT_VALUE;
  }

  bool isExternalPowered() override {
    if (isUsbPowered() || isSolarChargerActive()) {
      return true;
    }
    return NRF52Board::isExternalPowered();
  }

  bool isChargerActive() override {
    // Preserve a generic charging indication while also exposing both
    // source-specific states (USB / solar) separately through the CLI.
    return isSolarChargerActive() || isUsbPowered();
  }

  void powerOff() override;
};
