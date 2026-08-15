/*
 * variant.h - Seeed SenseCAP MeshTracker X1 (nRF52840 + Semtech LR2021)
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define USE_LFXO    // 32.768 kHz crystal oscillator
#define VARIANT_MCK (64000000ul)

////////////////////////////////////////////////////////////////////////////////
// Power

#define NRF_APM                                 // detect usb power
#define PIN_3V3_EN              (39)            // P1.7 Power to sensors
#define PIN_BAT_ADC_EN          (38)            // P1.6 Power to battery ADC divider
#define PIN_RTC_EN              (9)             // P0.9 Power to RTC

#define BATTERY_PIN             (2)             // P0.2/AIN0
#define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER          (2.0F)

#define EXT_CHRG_DETECT         (35)            // P1.3, LOW while charging
// P1.4 is the charger's second status line, but it reads LOW regardless of
// charge state on this board, so it is not used (Meshtastic leaves it out too)
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

#define PIN_SERIAL1_RX          (14)            // P0.14 (GPS)
#define PIN_SERIAL1_TX          (13)            // P0.13 (GPS)

#define PIN_SERIAL2_RX          (17)            // P0.17
#define PIN_SERIAL2_TX          (16)            // P0.16

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define HAS_WIRE                (1)
#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (47)            // P1.15
#define PIN_WIRE_SCL            (46)            // P1.14
#define I2C_NO_RESCAN

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (40)            // P1.8
#define PIN_SPI_MOSI            (41)            // P1.9
#define PIN_SPI_SCK             (11)            // P0.11
#define PIN_SPI_NSS             (12)            // P0.12

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs (RGB)

#define LED_BUILTIN             (-1)
#define LED_RED                 (3)             // P0.3
#define LED_GREEN               (24)            // P0.24
#define LED_BLUE                (28)            // P0.28
#define LED_PIN                 LED_GREEN

#define LED_STATE_ON            HIGH

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (6)             // P0.6, active HIGH (internal pull-down)
#define BUTTON_PIN              PIN_BUTTON1

////////////////////////////////////////////////////////////////////////////////
// LR2021

#define LORA_DIO_1              (33)            // P1.1, IRQ line (LR2021 DIO8)
#define LORA_NSS                (PIN_SPI_NSS)   // P0.12
#define LORA_RESET              (42)            // P1.10
#define LORA_BUSY               (7)             // P0.7
#define LORA_SCLK               (PIN_SPI_SCK)   // P0.11
#define LORA_MISO               (PIN_SPI_MISO)  // P1.8
#define LORA_MOSI               (PIN_SPI_MOSI)  // P1.9

////////////////////////////////////////////////////////////////////////////////
// GPS (Airoha, dual-band L1+L5)

#define HAS_GPS                 1
#define GPS_RX_PIN              PIN_SERIAL1_RX
#define GPS_TX_PIN              PIN_SERIAL1_TX
#define GPS_BAUD_RATE           (115200)

#define GPS_EN                  (43)            // P1.11, active HIGH
#define GPS_RESET               (8)             // P0.8, active HIGH

#define GPS_VRTC_EN             (45)            // P1.13
#define GPS_SLEEP_INT           (30)            // P0.30
#define GPS_RTC_INT             (29)            // P0.29

////////////////////////////////////////////////////////////////////////////////
// Haptic driver (DRV2605 on I2C)

#define PIN_DRV_EN              (37)            // P1.5, power to haptic driver

////////////////////////////////////////////////////////////////////////////////
// Buzzer

#define BUZZER_PIN              (25)            // P0.25, pwm output

////////////////////////////////////////////////////////////////////////////////
// QSPI Flash

#define PIN_FLASH_EN           (15)             // P0.15 (Flash power enable)

#define PIN_QSPI_SCK           (19)             // P0.19
#define PIN_QSPI_CS            (20)             // P0.20
#define PIN_QSPI_IO0           (21)             // P0.21
#define PIN_QSPI_IO1           (22)             // P0.22
#define PIN_QSPI_IO2           (23)             // P0.23
#define PIN_QSPI_IO3           (32)             // P1.00

#define EXTERNAL_FLASH_DEVICES  GD25Q64C