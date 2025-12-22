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

#define WIRE_INTERFACES_COUNT   (1)
////////////////////////////////////////////////////////////////////////////////
// Power

#define PIN_PWR_EN              (27)

#define BATTERY_PIN             (28)
#define ADC_MULTIPLIER          (1.75F)
#define PIN_ADC_CTRL            (11)

#define ADC_RESOLUTION          (12)
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

#define PIN_SERIAL2_RX          (22)
#define PIN_SERIAL2_TX          (24)

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define PIN_WIRE_SDA            (41)             // P1.9
#define PIN_WIRE_SCL            (8)             // P0.8

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (47)
#define PIN_SPI_MOSI            (46)
#define PIN_SPI_SCK             (45)
//#define PIN_SPI_NSS             (24)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define PIN_LED_RED             (12)
#define PIN_LED_BLUE            (7)
#define LED_BLUE                (-1)

#define LED_BUILTIN             PIN_LED_BLUE
#define PIN_LED                 LED_BUILTIN
#define LED_PIN                 LED_BUILTIN
#define LED_STATE_ON            HIGH

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (17)
#define BUTTON_PIN              PIN_BUTTON1
#define PIN_USER_BTN            BUTTON_PIN

////////////////////////////////////////////////////////////////////////////////
// QSPI

#define EXTERNAL_FLASH_DEVICES MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

#define PIN_QSPI_SCK (35)
#define PIN_QSPI_CS  (23)
#define PIN_QSPI_IO0 (33)  // MOSI if using two bit interface
#define PIN_QSPI_IO1 (34)  // MISO if using two bit interface
#define PIN_QSPI_IO2 (36)  // WP if using two bit interface (i.e. not used)
#define PIN_QSPI_IO3 (37)  // HOLD if using two bit interface (i.e. not used)
#define QSPI_FLASH_EN (21)

////////////////////////////////////////////////////////////////////////////////
// GPS

#define GPS_L76K
#define PIN_GPS_RX              (2)
#define PIN_GPS_TX              (3)
#define PIN_GPS_EN              (6)    // EN
#define PIN_GPS_RESET           (29)
#define PIN_GPS_STANDBY         (30)   // STANDBY
#define PIN_GPS_PPS             (31)
#define GPS_BAUD_RATE           9600
