/*
 * variant.h
 * Copyright (C) 2023 Seeed K.K.
 * MIT License
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

//#define USE_LFXO    // 32.768 kHz crystal oscillator
#define USE_LFRC    // RC oscillator
#define VARIANT_MCK (64000000ul)

#define WIRE_INTERFACES_COUNT 	(1)

////////////////////////////////////////////////////////////////////////////////
// Power

#define NRF_APM
#define PIN_3V3_EN              (0+13)

#define BATTERY_PIN             (0+31)


#define ADC_RESOLUTION          (14)
#define BATTERY_SENSE_RES       (12)

#define AREF_VOLTAGE            (3.0)

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (1)
#define NUM_ANALOG_OUTPUTS      (0)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_RX          (0+6)
#define PIN_SERIAL1_TX          (0+8)

#define PIN_SERIAL2_RX          (0+24)
#define PIN_SERIAL2_TX          (0+22)

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define PIN_WIRE_SDA            (32+6)             // P0.26
#define PIN_WIRE_SCL            (32+4)             // P0.27

//#define PIN_WIRE1_SDA            (0+22)
//#define PIN_WIRE1_SCL            (0+24)

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (32+15)
#define PIN_SPI_MOSI            (32+13)
#define PIN_SPI_SCK             (0+2)
#define PIN_SPI_NSS             (32+11)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_BUILTIN             (0+15)
#define PIN_LED                 LED_BUILTIN
#define LED_RED                 LED_BUILTIN
#define LED_BLUE                (-1)            // No blue led, prevents Bluefruit flashing the green LED during advertising
#define LED_PIN                 LED_BUILTIN
#define P_LORA_TX_LED           LED_BUILTIN
#define LED_STATE_ON            1

#define PIN_NEOPIXEL            (-1)
#define NEOPIXEL_NUM            (0)

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (32+1)
#define BUTTON_PIN              PIN_BUTTON1

#define PIN_BUTTON2             (32+2)
#define BUTTON_PIN2             PIN_BUTTON2

// #define PIN_USER_BTN            BUTTON_PIN

//#define EXTERNAL_FLASH_DEVICES MX25R1635F
//#define EXTERNAL_FLASH_USE_QSPI

////////////////////////////////////////////////////////////////////////////////
// Lora

#define USE_SX1262
#define LORA_CS                 PIN_SPI_NSS
#define SX126X_DIO1             (0+9)
#define SX126X_BUSY             (0+29)
#define SX126X_RESET            (0+10)
//#define SX126X_DIO2_AS_RF_SWITCH
//#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define PIN_SPI1_MISO           PIN_SPI_MISO
#define PIN_SPI1_MOSI           PIN_SPI_MOSI
#define PIN_SPI1_SCK            PIN_SPI_SCK

////////////////////////////////////////////////////////////////////////////////
// Buzzer

// #define PIN_BUZZER              (46)


////////////////////////////////////////////////////////////////////////////////
// GPS

#define GPS_EN                  (0+20)
#define GPS_RESET               (-1)

////////////////////////////////////////////////////////////////////////////////
// TFT //
/*#define PIN_TFT_SCL             (-1)
#define PIN_TFT_SDA             (-1)
#define PIN_TFT_RST             (-1)
#define PIN_TFT_VDD_CTL         (-1)
#define PIN_TFT_LEDA_CTL        (-1)
#define PIN_TFT_CS              (-1)
#define PIN_TFT_DC              (-1)
*/