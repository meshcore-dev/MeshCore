/*
 * variant.h - Seeed Wio WM1110 Dev Board
 * nRF52840 + LR1110 (LoRa + GNSS + WiFi Scanner)
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define USE_LFXO    // 32.768 kHz crystal oscillator
#define VARIANT_MCK (64000000ul)

////////////////////////////////////////////////////////////////////////////////
// Power

#define BATTERY_PIN             (31)            // AIN7
#define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER          (2.0F)

#define ADC_RESOLUTION          (14)
#define BATTERY_SENSE_RES       (12)

#define AREF_VOLTAGE            (3.0)

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (8)
#define NUM_ANALOG_OUTPUTS      (0)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_RX          (22)
#define PIN_SERIAL1_TX          (24)

#define PIN_SERIAL2_RX          (6)
#define PIN_SERIAL2_TX          (8)

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define HAS_WIRE                (1)
#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (27)
#define PIN_WIRE_SCL            (26)
#define I2C_NO_RESCAN

#define SENSOR_POWER_PIN        (7)

#define HAS_LIS3DH              (1)
#define LIS3DH_INT_PIN_1        (11)
#define LIS3DH_INT_PIN_2        (12)

#define HAS_SHT41               (1)

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (47)
#define PIN_SPI_MOSI            (46)
#define PIN_SPI_SCK             (45)
#define PIN_SPI_NSS             (44)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_BUILTIN             (13)
#define LED_GREEN               (13)
#define LED_RED                 (14)
#define LED_BLUE                LED_RED
#define LED_PIN                 LED_GREEN

#define LED_STATE_ON            HIGH

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (-1)
#define BUTTON_PIN              PIN_BUTTON1

////////////////////////////////////////////////////////////////////////////////
// LR1110 LoRa Radio + GNSS + WiFi

#define LORA_DIO_1              (40)            // P1.8 - LR1110_IRQ_PIN
#define LORA_NSS                (PIN_SPI_NSS)   // P1.12
#define LORA_RESET              (42)            // P1.10 - LR1110_NRESET_PIN
#define LORA_BUSY               (43)            // P1.11 - LR1110_BUSY_PIN
#define LORA_SCLK               (PIN_SPI_SCK)   // P1.13
#define LORA_MISO               (PIN_SPI_MISO)  // P1.15
#define LORA_MOSI               (PIN_SPI_MOSI)  // P1.14
#define LORA_CS                 PIN_SPI_NSS     // P1.12

// LR1110 specific settings
#define LR11X0_DIO_AS_RF_SWITCH    true
#define LR11X0_DIO3_TCXO_VOLTAGE   1.8
#define LR1110_GNSS_ANT_PIN        (37)         // P1.5

// Pin aliases for LR1110 driver compatibility
#define LR1110_IRQ_PIN          LORA_DIO_1
#define LR1110_NRESET_PIN       LORA_RESET
#define LR1110_BUSY_PIN         LORA_BUSY
#define LR1110_SPI_NSS_PIN      LORA_CS
#define LR1110_SPI_SCK_PIN      LORA_SCLK
#define LR1110_SPI_MOSI_PIN     LORA_MOSI
#define LR1110_SPI_MISO_PIN     LORA_MISO

////////////////////////////////////////////////////////////////////////////////
// Analog Input Pins

#define SENSOR_AIN_0            (2)
#define SENSOR_AIN_1            (3)
#define SENSOR_AIN_2            (4)
#define SENSOR_AIN_3            (5)
#define SENSOR_AIN_4            (28)
#define SENSOR_AIN_5            (29)
#define SENSOR_AIN_6            (30)
#define SENSOR_AIN_7            (31)

static const uint8_t A0  = SENSOR_AIN_0;
static const uint8_t A1  = SENSOR_AIN_1;
static const uint8_t A2  = SENSOR_AIN_2;
static const uint8_t A3  = SENSOR_AIN_3;
static const uint8_t A4  = SENSOR_AIN_4;
static const uint8_t A5  = SENSOR_AIN_5;
static const uint8_t A6  = SENSOR_AIN_6;
static const uint8_t A7  = SENSOR_AIN_7;

////////////////////////////////////////////////////////////////////////////////
// GPS/GNSS

#define HAS_GPS                 0
#define PIN_GPS_TX              (-1)
#define PIN_GPS_RX              (-1)
#define GPS_EN                  (-1)
#define GPS_RESET               (-1)

