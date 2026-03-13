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
// #define PIN_3V3_EN              (38)            // P1.6 Power to Sensors no power 

#define BATTERY_PIN (0 + 31) // P0.31
// #define BATTERY_CHARGING_INV (32 + 02) // P1.02
// #define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER          (1.537F)

#define EXT_CHRG_DETECT         (32+02)            // P1.02 Muzi Base schematic shows there's a bat_chg status. That's stat2 on BQ25. if low, it's charging. 
#define EXT_PWR_DETECT          (27)             // P0.5 stat1 (power detect, basically) on bq25 is connected to pin 0.27

#define ADC_RESOLUTION          (14)
#define BATTERY_SENSE_RES       (12)

#define AREF_VOLTAGE            (3.3)

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (6)
#define NUM_ANALOG_OUTPUTS      (0)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_RX          (19)             // P0.19 used for GPS RX
#define PIN_SERIAL1_TX          (20)             // P0.20 used for GPS TX


// #define PIN_SERIAL2_RX          (17)             // P0.17
// #define PIN_SERIAL2_TX          (16)             // P0.16

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define HAS_WIRE                (1)
#define WIRE_INTERFACES_COUNT   (2)

#define PIN_WIRE_SDA            (4)             // P0.4
#define PIN_WIRE_SCL            (6)             // P0.6
#define PIN_WIRE1_SDA           (24)             // P0.24 OLED I2C
#define PIN_WIRE1_SCL           (25)             // P0.25 OLED I2C
// #define I2C_NO_RESCAN
// #define HAS_QMA6100P
// #define QMA_6100P_INT_PIN       (34)             // P1.2

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (32+15)            // internally connected to p1.15
#define PIN_SPI_MOSI            (32+14)            // internally connected to p1.14
#define PIN_SPI_SCK             (32+13)            // internally connected to p1.13
#define PIN_SPI_NSS             (32+12)            // internally connected to p1.12

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_BUILTIN             (-1)
#define LED_BLUE                (32+4)            // P1.04
#define LED_GREEN               (32+3)            // P1.03
#define LED_PIN                 LED_GREEN

#define LED_STATE_ON            LOW

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (10)             // P0.10
#define BUTTON_PIN              PIN_BUTTON1

////////////////////////////////////////////////////////////////////////////////
// LR1110

#define LORA_DIO_1              (32+8)            // P1.08
#define LORA_NSS                (PIN_SPI_NSS)   // P1.12
#define LORA_RESET              (32+12)            // P1.12
#define LORA_BUSY               (32+11)             // P1.11
#define LORA_SCLK               (PIN_SPI_SCK) 
#define LORA_MISO               (PIN_SPI_MISO)
#define LORA_MOSI               (PIN_SPI_MOSI)
 
#define LR11X0_DIO_AS_RF_SWITCH    true
#define LR11X0_DIO3_TCXO_VOLTAGE   3.0

////////////////////////////////////////////////////////////////////////////////

#define PIN_QSPI_SCK (0 + 3)
#define PIN_QSPI_CS (0 + 26)
#define PIN_QSPI_IO0 (0 + 30)
#define PIN_QSPI_IO1 (0 + 29)
#define PIN_QSPI_IO2 (0 + 28)
#define PIN_QSPI_IO3 (0 + 2)

#define EXTERNAL_FLASH_DEVICES W25Q128JVPQ
#define EXTERNAL_FLASH_USE_QSPI
// // GPS

// #define HAS_GPS                 1
// #define GPS_RX_PIN              PIN_SERIAL1_RX
// #define GPS_TX_PIN              PIN_SERIAL1_TX

// #define GPS_EN                  (43)            // P1.11
// #define GPS_RESET               (47)            // P1.15

// #define GPS_VRTC_EN             (8)             // P0.8
// #define GPS_SLEEP_INT           (44)            // P1.12
// #define GPS_RTC_INT             (15)            // P0.15
// #define GPS_RESETB              (46)            // P1.14

////////////////////////////////////////////////////////////////////////////////
// Temp+Lux Sensor

// #define SENSOR_EN               (4)             // P0.4
// #define TEMP_SENSOR             (31)            // P0.31/AIN7
// #define LUX_SENSOR              (29)            // P0.29/AIN5

////////////////////////////////////////////////////////////////////////////////
// Accelerometer (I2C addr : ??? )

// #define PIN_3V3_ACC_EN          (39)            // P1.7

////////////////////////////////////////////////////////////////////////////////
// Buzzer

// #define BUZZER_EN               (37)            // P1.5
// #define BUZZER_PIN              (25)            // P0.25