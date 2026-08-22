/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <Arduino.h>
#include <CayenneLPP.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

// LoRa (SX1262)
#define P_LORA_DIO_1                 47
#define P_LORA_NSS                   42
#define P_LORA_RESET                 RADIOLIB_NC
#define P_LORA_BUSY                  46
#define P_LORA_SCLK                  43
#define P_LORA_MISO                  45
#define P_LORA_MOSI                  44
#define SX126X_POWER_EN              37  // P1.05, PE4259 RF switch VDD

#define SX126X_DIO2_AS_RF_SWITCH     true
#define SX126X_DIO3_TCXO_VOLTAGE     1.8

#define PIN_VBAT_READ                5
#define ADC_MULTIPLIER               (3 * 1.73 * 1.187 * 1000)

// Power management (INA228 + RV-3028)
#define RTC_INT_PIN                  17    // GPIO17 (WB_IO1)
#define RTC_I2C_ADDR                 0x52
#define INA228_I2C_ADDR              0x40  // A0=GND, A1=GND
#define BQ25798_I2C_ADDR             0x6B
#define BME280_I2C_ADDR              0x76

// RV-3028-C7 registers
#define RV3028_REG_STATUS            0x0E
#define RV3028_REG_CTRL1             0x0F
#define RV3028_REG_CTRL2             0x10
#define RV3028_REG_TIMER_VALUE_0     0x0A
#define RV3028_REG_TIMER_VALUE_1     0x0B

// GPREGRET2 layout: [1:0] shutdown reason, [7:2] state flags
#define SHUTDOWN_REASON_NONE         0x00
#define SHUTDOWN_REASON_LOW_VOLTAGE  0x01
#define SHUTDOWN_REASON_USER_REQUEST 0x02
#define SHUTDOWN_REASON_THERMAL      0x03
#define GPREGRET2_LOW_VOLTAGE_SLEEP  0x04

#define LOW_VOLTAGE_SLEEP_MINUTES    (60)

class InheroMr2Board : public NRF52BoardDCDC {
public:
  InheroMr2Board() : NRF52Board("InheroMR2_OTA") {}
  void begin();
  void tick() override;

  uint16_t getBattMilliVolts() override;

  void initiateShutdown(uint8_t reason);
  void configureRTCWake(uint32_t minutes);
  uint16_t getLowVoltageSleepThreshold();
  uint16_t getLowVoltageWakeThreshold();

  static void rtcInterruptHandler();

  const char *getManufacturerName() const override { return "Inhero MR2"; }
  void reboot() override { NVIC_SystemReset(); }

  bool startOTAUpdate(const char *id, char reply[]) override;
  bool getCustomGetter(const char *getCommand, char *reply, uint32_t maxlen) override;
  const char *setCustomSetter(const char *setCommand) override;
  bool queryBoardTelemetry(CayenneLPP &telemetry) override;

private:
  static volatile bool rtc_irq_pending;
  static volatile uint32_t ota_dfu_reset_at;  // millis() of deferred DFU reset (0 = inactive)
};
