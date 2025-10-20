/*
 * variant.h
 * Copyright (C) 2023 Seeed K.K.
 * MIT License
 */

#pragma once

#define _PINNUM(port, pin) ((port) * 32 + (pin))

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define USE_LFXO    // 32.768 kHz crystal oscillator
#define VARIANT_MCK (64000000ul)

#define WIRE_INTERFACES_COUNT   (1)

////////////////////////////////////////////////////////////////////////////////
// Power

#define PIN_PWR_EN              _PINNUM(0, 30) // RT9080_EN

#define BATTERY_PIN             _PINNUM(0, 2)
#define ADC_MULTIPLIER          (4.90F)

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

#define PIN_SERIAL1_RX          PIN_GPS_TX
#define PIN_SERIAL1_TX          PIN_GPS_RX
////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define PIN_WIRE_SDA            _PINNUM(0, 4) // (SDA)
#define PIN_WIRE_SCL            _PINNUM(0, 2) // (SCL)

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    _PINNUM(0, 2)

#define PIN_SPI_MISO            _PINNUM(0, 17) // (MISO)
#define PIN_SPI_MOSI            _PINNUM(0, 15) // (MOSI)
#define PIN_SPI_SCK             _PINNUM(0, 13) // (SCK)
#define PIN_SPI_NSS             (-1)

////////////////////////////////////////////////////////////////////////////////
// QSPI FLASH

#define PIN_QSPI_SCK            _PINNUM(0, 4)
#define PIN_QSPI_CS             _PINNUM(0, 12)
#define PIN_QSPI_IO0            _PINNUM(0, 6)
#define PIN_QSPI_IO1            _PINNUM(0, 8)
#define PIN_QSPI_IO2            _PINNUM(1, 9)
#define PIN_QSPI_IO3            _PINNUM(0, 26)

#define EXTERNAL_FLASH_DEVICES ZD25WQ32CEIGR
#define EXTERNAL_FLASH_USE_QSPI

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_RED                 _PINNUM(1, 14) // LED_3
#define LED_BLUE                _PINNUM(1, 5)  // LED_2
#define LED_GREEN               _PINNUM(1, 7)  // LED_1

//#define PIN_STATUS_LED          LED_BLUE
#define LED_BUILTIN             (-1)
#define LED_PIN                 LED_BUILTIN
#define LED_STATE_ON            LOW

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             _PINNUM(0, 24) // BOOT
#define BUTTON_PIN              PIN_BUTTON1
#define PIN_USER_BTN            BUTTON_PIN

#define PIN_BUTTON2             _PINNUM(0, 18)
#define BUTTON_PIN2             PIN_BUTTON2

#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

////////////////////////////////////////////////////////////////////////////////
// Lora

#define USE_SX1262
#define LORA_CS                 _PINNUM(0, 11)
#define SX126X_POWER_EN         _PINNUM(0, 30)
#define SX126X_DIO1             _PINNUM(1, 8)
#define SX126X_BUSY             _PINNUM(0, 14)
#define SX126X_RESET            _PINNUM(0, 7)
#define SX126X_RF_VC1           _PINNUM(0, 27)
#define SX126X_RF_VC2           _PINNUM(0, 33)

#define P_LORA_DIO_1            SX126X_DIO1
#define P_LORA_NSS              LORA_CS
#define P_LORA_RESET            SX126X_RESET
#define P_LORA_BUSY             SX126X_BUSY
#define P_LORA_SCLK             PIN_SPI_SCK
#define P_LORA_MISO             PIN_SPI_MISO
#define P_LORA_MOSI             PIN_SPI_MOSI

////////////////////////////////////////////////////////////////////////////////
// SPI1

#define PIN_SPI1_MISO           (-1)            // Not used for Display
#define PIN_SPI1_MOSI           _PINNUM(0, 20)
#define PIN_SPI1_SCK            _PINNUM(0, 19)

// GxEPD2 needs that for a panel that is not even used !
extern const int MISO;
extern const int MOSI;
extern const int SCK;

////////////////////////////////////////////////////////////////////////////////
// Display

// #define DISP_MISO               (-1)         // Not used for Display
#define DISP_MOSI               _PINNUM(0, 20)
#define DISP_SCLK               _PINNUM(0, 19)
#define DISP_CS                 _PINNUM(0, 22)
#define DISP_DC                 _PINNUM(0, 21)
#define DISP_RST                _PINNUM(0, 28)
#define DISP_BUSY               _PINNUM(0, 3)
#define DISP_POWER              _PINNUM(1, 12)
// #define DISP_BACKLIGHT          (-1)         // Display has no backlight

#define PIN_DISPLAY_CS          DISP_CS
#define PIN_DISPLAY_DC          DISP_DC
#define PIN_DISPLAY_RST         DISP_RST
#define PIN_DISPLAY_BUSY        DISP_BUSY

////////////////////////////////////////////////////////////////////////////////
// GPS

#define PIN_GPS_RX              _PINNUM(1, 13) // RXD
#define PIN_GPS_TX              _PINNUM(1, 15) // TXD
#define GPS_EN                  _PINNUM(1, 11) // POWER_RT9080_EN
#define PIN_GPS_STANDBY         _PINNUM(1, 10)
#define PIN_GPS_PPS             _PINNUM(0, 29) // 1PPS
