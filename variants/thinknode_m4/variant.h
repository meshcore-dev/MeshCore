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
// #define USE_LFRC    // 32.768 kHz RC oscillator

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (1)
#define NUM_ANALOG_OUTPUTS      (0)

////////////////////////////////////////////////////////////////////////////////
// Power

#define NRF_APM                                 // detect usb power

#define EXT_CHRG_DETECT         (38)
// Power to radio
#define PIN_PWR_EN              (11)

// I2C bus power
#define I2C_POWER               (32)
#define I2C_POWER_ACTIVE        LOW

#define PIN_BAT_RX              (5)
#define PIN_BAT_TX              (30)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_RX          PIN_GPS_TX
#define PIN_SERIAL1_TX          PIN_GPS_RX

#define PIN_SERIAL2_RX          PIN_BAT_TX
#define PIN_SERIAL2_TX          PIN_BAT_RX

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define HAS_WIRE                (1)
#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (23)
#define PIN_WIRE_SCL            (25)
#define I2C_NO_RESCAN

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (8)
#define PIN_SPI_MOSI            (7)
#define PIN_SPI_SCK             (6)
#define PIN_SPI_NSS             (27)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_BLUE                (-1)            // disable blue led
#define LED_STATUS              (13)            // blue
#define LED_PIN                 (41)            // red
#define LED_TX                  LED_PIN
#define LED_BUILTIN             LED_BLUE
#define LED_STATE_ON            HIGH

#define LED_BAT1                (15)
#define LED_BAT2                (17)
#define LED_BAT3                (34)
#define LED_BAT4                (36)

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (4)
#define BUTTON_PIN              PIN_BUTTON1
#define USER_BTN_PRESSED        HIGH

////////////////////////////////////////////////////////////////////////////////
// GPS

#define HAS_GPS                 1
#define GPS_BAUDRATE            9600
#define PIN_GPS_RX              (44)
#define PIN_GPS_TX              (46)

#define PIN_GPS_POWER           (14)
#define GPS_POWER_ACTIVE        LOW
#define PIN_GPS_EN              (43)
#define GPS_EN_ACTIVE           LOW
#define PIN_GPS_RESET           (3)
#define GPS_RESET_ACTIVE        HIGH
