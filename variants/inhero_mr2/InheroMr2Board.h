/*
 * Copyright (c) 2026 Inhero GmbH
 *
 * SPDX-License-Identifier: MIT
 *
 * Inhero MR-2 Board Class
 */
#pragma once

#include <Arduino.h>
#include <CayenneLPP.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

// LoRa radio module pins for Inhero MR-2
#define P_LORA_DIO_1                 47
#define P_LORA_NSS                   42
#define P_LORA_RESET                 RADIOLIB_NC // 38
#define P_LORA_BUSY                  46
#define P_LORA_SCLK                  43
#define P_LORA_MISO                  45
#define P_LORA_MOSI                  44
#define SX126X_POWER_EN              37 // P1.05 — PE4259 RF switch VDD (power enable)

// GPS module support (future expansion)
// Note: GPS pins not yet configured in MR2 hardware
// GPS_BAUD_RATE would be 9600, GPS_ADDRESS would be 0x42 (I2C)

#define SX126X_DIO2_AS_RF_SWITCH     true
#define SX126X_DIO3_TCXO_VOLTAGE     1.8

// built-ins
#define PIN_VBAT_READ                5
#define ADC_MULTIPLIER               (3 * 1.73 * 1.187 * 1000)

// Power Management Configuration (INA228 + RTC)
// Note: GPIO17 (WB_IO1) is used for RTC_INT, not available for GPS_1PPS
#define RTC_INT_PIN                  17   // GPIO17 (WB_IO1) - RTC Interrupt from RV-3028
#define RTC_I2C_ADDR                 0x52 // RV-3028-C7 I2C address
#define INA228_I2C_ADDR              0x40 // INA228 I2C address (A0=GND, A1=GND)
#define BQ25798_I2C_ADDR             0x6B // BQ25798 Battery Charger I2C address
// Note: INA228 ALERT pin (P1.02) triggers low-voltage sleep via interrupt
// TPS62840 EN tied to VDD (always on) — no hardware UVLO cutoff

// RV-3028-C7 RTC Register Addresses (Per Application Manual Section 3.2)
#define RV3028_REG_STATUS            0x0E // Status register (TF flag at bit 3)
#define RV3028_REG_CTRL1             0x0F // Control 1 (TE at bit 2, TD at bits 1:0)
#define RV3028_REG_CTRL2             0x10 // Control 2 (TIE at bit 4)
#define RV3028_REG_TIMER_VALUE_0     0x0A // Timer Value 0 (lower 8 bits)
#define RV3028_REG_TIMER_VALUE_1     0x0B // Timer Value 1 (upper 4 bits)

// Shutdown reason codes (stored in GPREGRET2 bits [1:0])
#define SHUTDOWN_REASON_NONE         0x00
#define SHUTDOWN_REASON_LOW_VOLTAGE  0x01
#define SHUTDOWN_REASON_USER_REQUEST 0x02
#define SHUTDOWN_REASON_THERMAL      0x03

// Power management state flags (stored in GPREGRET2 bits [7:2])
#define GPREGRET2_LOW_VOLTAGE_SLEEP  0x04 // Bit 2: In low-voltage sleep (RTC wake cycle)

// Low-voltage sleep duration (used by Early Boot and SOC update)
#define LOW_VOLTAGE_SLEEP_MINUTES    (60)

class InheroMr2Board : public NRF52BoardDCDC {
public:
  InheroMr2Board() : NRF52Board("InheroMR2_OTA") {}
  void begin();
  void tick() override; // Feed watchdog and perform board-specific tasks

  uint16_t getBattMilliVolts() override;

  // Power Management Methods (INA228 + RTC)
  /// @brief Initiate controlled shutdown with filesystem protection
  /// @param reason Shutdown reason code (stored in GPREGRET2 for next boot)
  void initiateShutdown(uint8_t reason);

  /// @brief Configure RV-3028 RTC countdown timer for periodic wake-up
  /// @param minutes Wake-up interval in minutes
  void configureRTCWake(uint32_t minutes);

  /// @brief Get voltage threshold for low-voltage sleep (chemistry-specific)
  /// @return Sleep threshold in millivolts (INA228 ALERT fires here)
  uint16_t getLowVoltageSleepThreshold();

  /// @brief Get voltage threshold for low-voltage wake / 0% SOC (chemistry-specific)
  /// @return Wake threshold in millivolts
  uint16_t getLowVoltageWakeThreshold();

  /// @brief RTC interrupt handler (called by hardware interrupt)
  static void rtcInterruptHandler();

  /// @brief Put SX1262 + SPI pins into lowest power state for System Sleep
  /// @param radioInitialized false in Early Boot (before SPI.begin), true after full init
  /// Must be called before any sd_power_system_off() to prevent ~4mA SX1262 leakage
  static void prepareRadioForSystemOff(bool radioInitialized = true);

  /// @brief Disconnect internal pull-ups on OD/I2C pins to prevent leakage in System Sleep
  /// Must be called AFTER Wire.end() and before sd_power_system_off()
  static void disconnectLeakyPullups();

  const char *getManufacturerName() const override { return "Inhero MR2"; }

  void reboot() override { NVIC_SystemReset(); }

  bool startOTAUpdate(const char *id, char reply[]) override;

  bool getCustomGetter(const char *getCommand, char *reply, uint32_t maxlen) override;
  const char *setCustomSetter(const char *setCommand) override;
  bool queryBoardTelemetry(CayenneLPP &telemetry) override;

private:
  uint8_t findNextFreeChannel(CayenneLPP &lpp);
  static uint16_t socToLiIonMilliVolts(float soc_percent);
  static volatile bool rtc_irq_pending;
  static volatile uint32_t ota_dfu_reset_at; ///< millis() timestamp for deferred DFU reset (0 = inactive)
};
