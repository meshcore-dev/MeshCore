/*
 * variant.h
 * Copyright (C) 2023 Seeed K.K.
 * MIT License
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define USE_LFXO    // 32.768 kHz crystal oscillator
#define VARIANT_MCK (64000000ul)

#define WIRE_INTERFACES_COUNT 	(2)

////////////////////////////////////////////////////////////////////////////////
// Power

#define NRF_APM
#define PIN_3V3_EN              (38)

#define BATTERY_PIN             (3)
#define ADC_MULTIPLIER          (4.90F)

#define ADC_RESOLUTION          (14)
#define BATTERY_SENSE_RES       (12)

#define AREF_VOLTAGE            (3.0)
#define MV_LSB   (3000.0F / 4096.0F) // 12-bit ADC with 3.0V input range

// Power management boot protection threshold (millivolts)
// Set to 0 to disable boot protection
#define PWRMGT_VOLTAGE_BOOTLOCK 3300   // Won't boot below this voltage (mV)
// LPCOMP wake configuration (voltage recovery from SYSTEMOFF)
// AIN1 = P0.03 = BATTERY_PIN / PIN_VBAT_READ
#define PWRMGT_LPCOMP_AIN    1
#define PWRMGT_LPCOMP_REFSEL 1  // 2/8 VDD (~3.68-4.04V)

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (1)
#define NUM_ANALOG_OUTPUTS      (0)


// I2C pin definition

#define PIN_WIRE_SDA (0 + 7)
#define PIN_WIRE_SCL (0 + 8)

// I2C bus 1
// Available on header pins, for general use
#define PIN_WIRE1_SDA (0 + 4)
#define PIN_WIRE1_SCL (0 + 27)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_BUILTIN             (28)
#define PIN_LED                 LED_BUILTIN
#define LED_RED                 LED_BUILTIN
#define LED_BLUE                (-1)            // No blue led, prevents Bluefruit flashing the green LED during advertising
#define LED_PIN                 LED_BUILTIN

#define LED_STATE_ON            1

// #define PIN_NEOPIXEL            (-1)
// #define NEOPIXEL_NUM            (2)

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (32 + 10)
#define BUTTON_PIN              PIN_BUTTON1

// #define PIN_BUTTON2             (11)
// #define BUTTON_PIN2             PIN_BUTTON2

#define PIN_USER_BTN            BUTTON_PIN

////////////////////////////////////////////////////////////////////////////////
// Lora

#define USE_SX1262
#define LORA_CS                 (0 + 5)
#define SX126X_DIO1             (0 + 21)
#define SX126X_BUSY             (0 + 19)
#define SX126X_RESET            (0 + 16)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (2)

#define PIN_SPI_MISO            (0 + 14)
#define PIN_SPI_MOSI            (0 + 11)
#define PIN_SPI_SCK             (32 + 8)
#define PIN_SPI_NSS             LORA_CS

#define PIN_SPI1_MISO           (-1)
#define PIN_SPI1_MOSI           (0+17)
#define PIN_SPI1_SCK            (0+20)

////////////////////////////////////////////////////////////////////////////////
// GPS

#define GPS_EN                  (0 + 6)
#define GPS_RESET               (32 + 14)

#define PIN_SERIAL1_RX          (0 + 23)
#define PIN_SERIAL1_TX          (0 + 25)

#define PIN_SERIAL2_RX          (0 + 9)
#define PIN_SERIAL2_TX          (0 + 10)

////////////////////////////////////////////////////////////////////////////////
// TFT
#define PIN_TFT_SCL             (0 + 20)
#define PIN_TFT_SDA             (0 + 17)
#define PIN_TFT_RST             (0 + 13)
// #define PIN_TFT_VDD_CTL         (0 + 26)
#define PIN_TFT_LEDA_CTL        (32 + 12)
#define PIN_TFT_LEDA_CTL_ACTIVE LOW
#define PIN_TFT_CS              (0 + 22)
#define PIN_TFT_DC              (0 + 15)
