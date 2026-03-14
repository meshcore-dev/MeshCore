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
// Power

#define NRF_APM                                 // detect usb power
#define PIN_3V3_EN              (38)            // P1.6 Power to Sensors

#define BATTERY_PIN             (2)             // P0.2/AIN0
#define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER          (2.0F)

#define EXT_CHRG_DETECT         (35)            // P1.3
#define EXT_PWR_DETECT          (5)             // P0.5

#define ADC_RESOLUTION          (14)
#define BATTERY_SENSE_RES       (12)

#define AREF_VOLTAGE            (3.0)

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (6)
#define NUM_ANALOG_OUTPUTS      (0)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_RX          (14)             // P0.14
#define PIN_SERIAL1_TX          (13)             // P0.13

#define PIN_SERIAL2_RX          (17)             // P0.17
#define PIN_SERIAL2_TX          (16)             // P0.16

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define HAS_WIRE                (1)
#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (26)             // P0.26
#define PIN_WIRE_SCL            (27)             // P0.27
#define I2C_NO_RESCAN
#define HAS_QMA6100P
#define QMA_6100P_INT_PIN       (34)             // P1.2

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (40)            // P1.8
#define PIN_SPI_MOSI            (41)            // P1.9
#define PIN_SPI_SCK             (11)            // P0.11
#define PIN_SPI_NSS             (12)            // P0.12

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_BUILTIN             (-1)
#define LED_BLUE                (-1)            // No blue led
#define LED_GREEN               (24)            // P0.24
#define LED_PIN                 LED_GREEN

#define LED_STATE_ON            HIGH

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (6)             // P0.6
#define BUTTON_PIN              PIN_BUTTON1

////////////////////////////////////////////////////////////////////////////////
// LR1110

#define LORA_DIO_1              (33)            // P1.1
#define LORA_NSS                (PIN_SPI_NSS)   // P0.12
#define LORA_RESET              (42)            // P1.10
#define LORA_BUSY               (7)             // P0.7
#define LORA_SCLK               (PIN_SPI_SCK)   // P0.11
#define LORA_MISO               (PIN_SPI_MISO)  // P1.8
#define LORA_MOSI               (PIN_SPI_MOSI)  // P0.9

#define LR11X0_DIO_AS_RF_SWITCH    true
#define LR11X0_DIO3_TCXO_VOLTAGE   1.6

////////////////////////////////////////////////////////////////////////////////
// GPS

#define HAS_GPS                 1
#define GPS_RX_PIN              PIN_SERIAL1_RX
#define GPS_TX_PIN              PIN_SERIAL1_TX

#define GPS_EN                  (43)            // P1.11
#define GPS_RESET               (47)            // P1.15

#define GPS_VRTC_EN             (8)             // P0.8
#define GPS_SLEEP_INT           (44)            // P1.12
#define GPS_RTC_INT             (15)            // P0.15
#define GPS_RESETB              (46)            // P1.14

////////////////////////////////////////////////////////////////////////////////
// Temp+Lux Sensor

#define SENSOR_EN               (4)             // P0.4
#define TEMP_SENSOR             (31)            // P0.31/AIN7
#define LUX_SENSOR              (29)            // P0.29/AIN5

////////////////////////////////////////////////////////////////////////////////
// Accelerometer (I2C addr : ??? )

#define PIN_3V3_ACC_EN          (39)            // P1.7

////////////////////////////////////////////////////////////////////////////////
// Buzzer

#define BUZZER_EN               (37)            // P1.5
#define BUZZER_PIN              (25)            // P0.25
