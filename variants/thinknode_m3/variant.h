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


#define EXT_CHRG_DETECT         (32)            // P1.3
#define EXT_PWR_DETECT          (31)             // P0.5

#define PIN_VBAT_READ           (5) 
#define AREF_VOLTAGE            (2.4f)
#define ADC_MULTIPLIER          (2.0)           //(1.75f)
// 2.0 gives more coherent value, 4.2V when charged, needs tweaking
#define ADC_RESOLUTION          (12)
#define ADC_MAX                 (4096)

#define EEPROM_POWER            (7)
#define BAT_POWER               (17)
#define PIN_PWR_EN              (16)


////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_RX          PIN_GPS_TX
#define PIN_SERIAL1_TX          PIN_GPS_RX

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define HAS_WIRE                (1)
#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (26)             // P0.26
#define PIN_WIRE_SCL            (27)             // P0.27
#define I2C_NO_RESCAN

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (47)            // P1.15
#define PIN_SPI_MOSI            (46)            // P1.14
#define PIN_SPI_SCK             (45)            // P1.13
#define PIN_SPI_NSS             (44)            // P1.12

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_POWER               (29)
#define LED_BLUE                (-1)            // No blue led
#define PIN_LED_BLUE            (37)
#define PIN_LED_GREEN           (35)            // P0.24
#define PIN_LED_RED             (33)
#define LED_PIN                 PIN_LED_GREEN
#define LED_BUILTIN             PIN_LED_BLUE
#define LED_STATE_ON            LOW

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (12)             // P0.12
#define BUTTON_PIN              PIN_BUTTON1

////////////////////////////////////////////////////////////////////////////////
// GPS

#define HAS_GPS                     1
#define PIN_GPS_RX                  (22)    
#define PIN_GPS_TX                  (20)

#define PIN_GPS_POWER               (14)
#define PIN_GPS_EN                  (21)            // STANDBY
#define PIN_GPS_RESET               (25)            // REINIT
#define GPS_RESET_ACTIVE            LOW
#define GPS_EN_ACTIVE               HIGH
#define GPS_BAUDRATE                9600

////////////////////////////////////////////////////////////////////////////////
// Buzzer

#define BUZZER_EN               (37)            // P1.5
#define BUZZER_PIN              (25)            // P0.25