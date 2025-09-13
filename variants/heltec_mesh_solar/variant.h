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


////////////////////////////////////////////////////////////////////////////////
// Power

#define NRF_APM

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (1)
#define NUM_ANALOG_OUTPUTS      (0)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_RX          (37)
#define PIN_SERIAL1_TX          (39)

#define PIN_SERIAL2_RX          (9)
#define PIN_SERIAL2_TX          (10)

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition
#define WIRE_INTERFACES_COUNT 	(2)

#define PIN_WIRE_SDA            (6) 
#define PIN_WIRE_SCL            (26)

#define PIN_WIRE1_SDA           (30) 
#define PIN_WIRE1_SCL           (5)

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (2)

#define PIN_SPI_MISO            (23)
#define PIN_SPI_MOSI            (22)
#define PIN_SPI_SCK             (19)
#define PIN_SPI_NSS             (24)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_BUILTIN             (12)
#define PIN_LED                 LED_BUILTIN
#define LED_RED                 LED_BUILTIN
#define LED_BLUE                (-1)            // No blue led, prevents Bluefruit flashing the green LED during advertising
#define LED_PIN                 LED_BUILTIN

#define LED_STATE_ON            LOW

#define PIN_NEOPIXEL            (47)
#define NEOPIXEL_NUM            (1)

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (42)
#define BUTTON_PIN              PIN_BUTTON1

// #define PIN_BUTTON2             (11)
// #define BUTTON_PIN2             PIN_BUTTON2

#define PIN_USER_BTN            BUTTON_PIN

#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

////////////////////////////////////////////////////////////////////////////////
// Lora

#define USE_SX1262
#define LORA_CS                 (24)
#define SX126X_DIO1             (20)
#define SX126X_BUSY             (17)
#define SX126X_RESET            (25)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define PIN_SPI1_MISO           (43)
#define PIN_SPI1_MOSI           (41)
#define PIN_SPI1_SCK            (40)

////////////////////////////////////////////////////////////////////////////////
// Buzzer

// #define PIN_BUZZER              (46)


////////////////////////////////////////////////////////////////////////////////
// GPS

#define GPS_RESET               (38)

////////////////////////////////////////////////////////////////////////////////
// TFT
// #define PIN_TFT_SCL             (40)
// #define PIN_TFT_SDA             (41)
// #define PIN_TFT_RST             (2)
// #define PIN_TFT_VDD_CTL         (3)
// #define PIN_TFT_LEDA_CTL        (15)
// #define PIN_TFT_CS              (11)
// #define PIN_TFT_DC              (12)

////////////////////////////////////////////////////////////////////////////////
#define BQ4050_SDA_PIN                      (33) // I2C data line pin
#define BQ4050_SCL_PIN                      (32) // I2C clock line pin
#define BQ4050_EMERGENCY_SHUTDOWN_PIN       (35) // Emergency shutdown pin