/*
 * variant.h - MuziWorks Base Duo
 * nRF52840 + LR1121 (Sub-GHz LoRa)
 * Elecrow nRFLR1121 module
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define USE_LFXO    // 32.768 kHz crystal oscillator
#define VARIANT_MCK (64000000ul)

////////////////////////////////////////////////////////////////////////////////
// Power

#define BATTERY_PIN             (31)            // P0.31 (AIN7)
#define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER          (1.537F)

#define ADC_RESOLUTION          (12)
#define BATTERY_SENSE_RES       (12)

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (8)
#define NUM_ANALOG_OUTPUTS      (0)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_RX          (20)            // P0.20 (GPS RX)
#define PIN_SERIAL1_TX          (19)            // P0.19 (GPS TX)

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define HAS_WIRE                (1)
#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (24)            // P0.24
#define PIN_WIRE_SCL            (25)            // P0.25
#define I2C_NO_RESCAN

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition (internal to Elecrow nRFLR1121 module)

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (47)            // P1.15
#define PIN_SPI_MOSI            (46)            // P1.14
#define PIN_SPI_SCK             (45)            // P1.13
#define PIN_SPI_NSS             (44)            // P1.12

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs (active LOW)

#define LED_GREEN               (35)            // P1.03
#define LED_BLUE                (36)            // P1.04
#define LED_BUILTIN             LED_GREEN
#define LED_PIN                 LED_GREEN

#define LED_STATE_ON            LOW

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (-1)
#define BUTTON_PIN              PIN_BUTTON1

////////////////////////////////////////////////////////////////////////////////
// LR1121 LoRa Radio (sub-GHz)

#define LORA_DIO_1              (40)            // P1.08 - LR1121 IRQ (DIO1)
#define LORA_NSS                (PIN_SPI_NSS)   // P1.12
#define LORA_RESET              (42)            // P1.10 - LR1121 NRESET
#define LORA_BUSY               (43)            // P1.11 - LR1121 BUSY
#define LORA_SCLK               (PIN_SPI_SCK)   // P1.13
#define LORA_MISO               (PIN_SPI_MISO)  // P1.15
#define LORA_MOSI               (PIN_SPI_MOSI)  // P1.14
#define LORA_CS                 PIN_SPI_NSS     // P1.12

#define LR11X0_DIO_AS_RF_SWITCH    true
#define LR11X0_DIO3_TCXO_VOLTAGE   3.0

////////////////////////////////////////////////////////////////////////////////
// GPS/GNSS

#define HAS_GPS                 0
#define PIN_GPS_TX              (-1)
#define PIN_GPS_RX              (-1)
#define GPS_EN                  (-1)
#define GPS_RESET               (-1)

////////////////////////////////////////////////////////////////////////////////
// QSPI Flash (W25Q32JVSS - 8MB)

#define PIN_QSPI_SCK            (3)             // P0.03
#define PIN_QSPI_CS             (26)            // P0.26
#define PIN_QSPI_IO0            (30)            // P0.30
#define PIN_QSPI_IO1            (29)            // P0.29
#define PIN_QSPI_IO2            (28)            // P0.28
#define PIN_QSPI_IO3            (2)             // P0.02

#define EXTERNAL_FLASH_DEVICES  W25Q64JV
#define EXTERNAL_FLASH_USE_QSPI

////////////////////////////////////////////////////////////////////////////////
// Additional peripheral pins (not enabled)
//
// I2C bus 1 (IMU):       SDA P0.04, SCL P0.06
// Display (SH1107 OLED): I2C bus 0, power enable P0.23
// Trackball:             UP P0.21, DOWN P0.17, LEFT P1.05, RIGHT P0.16, PRESS P0.10
// Buzzer:                P0.22
// Buttons:               Cancel P0.15, Mode1 P1.09, Mode2 P0.12
// GPS:                   RX P0.20, TX P0.19, EN P1.01
// Charge status:         P1.02 (inverted)
