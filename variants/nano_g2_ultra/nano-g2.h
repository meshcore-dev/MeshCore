#pragma once

#include "variant.h"

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

// LoRa radio module pins
#define P_LORA_DIO_1             (32 + 10)
#define P_LORA_NSS               (32 + 13)
#define P_LORA_RESET             (32 + 15)
#define P_LORA_BUSY              (32 + 11)
#define P_LORA_SCLK              (0 + 12)
#define P_LORA_MISO              (32 + 9)
#define P_LORA_MOSI              (0 + 11)

#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define SX126X_POWER_EN          37

// buttons
#define PIN_BUTTON1              (32 + 6)
#define BUTTON_PIN               PIN_BUTTON1
#define PIN_USER_BTN             BUTTON_PIN

// GPS
#define GPS_EN                   PIN_GPS_STANDBY
// built-ins
#define VBAT_MV_PER_LSB          (0.73242188F) // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096

#define VBAT_DIVIDER             (0.5F) // 150K + 150K voltage divider on VBAT, actually 100K + 100K
#define VBAT_DIVIDER_COMP        (2.0F) // Compensation factor for the VBAT divider

#define PIN_VBAT_READ            (0 + 2)
#define REAL_VBAT_MV_PER_LSB     (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

class NanoG2Ultra : public NRF52Board {
public:
  NanoG2Ultra() : NRF52Board("NANO_G2_OTA") {}
  void begin();
  uint16_t getBattMilliVolts() override;

  const char *getManufacturerName() const override { return "Nano G2 Ultra"; }

  void powerOff() override {
    // put GPS chip to sleep
    digitalWrite(PIN_GPS_STANDBY, LOW);
    // TODO: unset buzzer to prevent notification circuit activating on hibernate
    // needs to be set as silent or somehow stop using macros for pins

    nrf_gpio_cfg_sense_input(digitalPinToInterrupt(PIN_USER_BTN), NRF_GPIO_PIN_NOPULL,
                             NRF_GPIO_PIN_SENSE_LOW);

    sd_power_system_off();
  }
};
