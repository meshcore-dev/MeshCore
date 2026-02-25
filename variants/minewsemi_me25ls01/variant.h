#pragma once

#include "WVariant.h"

// Low frequency clock source
#define USE_LFXO    // 32.768 kHz crystal oscillator
#define VARIANT_MCK (64000000ul)
// #define USE_LFRC    // 32.768 kHz RC oscillator

// Power
#define BATTERY_PIN (31)
#define BATTERY_IMMUTABLE
#define ADC_MULTIPLIER        (2.0F)

#define ADC_RESOLUTION        (14)
#define BATTERY_SENSE_RES     (12)

// Number of pins
#define PINS_COUNT            (48)
#define NUM_DIGITAL_PINS      (48)
#define NUM_ANALOG_INPUTS     (6)
#define NUM_ANALOG_OUTPUTS    (0)

// UART pin definition
#define PIN_SERIAL1_RX        (14) // P0.14
#define PIN_SERIAL1_TX        (13) // P0.13

#define PIN_SERIAL2_RX        (15) // P0.15
#define PIN_SERIAL2_TX        (17) // P0.17

// I2C pin definition
#define HAS_WIRE              (1)
#define WIRE_INTERFACES_COUNT (1)

#define PIN_WIRE_SDA          (21) // P0.21
#define PIN_WIRE_SCL          (16) // P0.16
#define I2C_NO_RESCAN

// SPI pin definition
#define SPI_INTERFACES_COUNT     (1)

#define PIN_SPI_MISO             (0 + 29)  // P0.29
#define PIN_SPI_MOSI             (0 + 2)   // P0.2
#define PIN_SPI_SCK              (32 + 15) // P1.15
#define PIN_SPI_NSS              (32 + 13) // P1.13

// Builtin LEDs
#define LED_BUILTIN              (-1)
#define LED_RED                  (32 + 5) // P1.5
#define LED_BLUE                 (32 + 7) // P1.7
#define LED_PIN                  LED_BLUE
#define P_LORA_TX_LED            LED_RED

#define LED_STATE_ON             HIGH

// Builtin buttons

#define PIN_BUTTON1              (0 + 27) // P0.6
#define BUTTON_PIN               PIN_BUTTON1

// LR1110
#define LORA_DIO_1               (32 + 12)      // P1.12
#define LORA_DIO_2               (32 + 10)      // P1.10
#define LORA_NSS                 (PIN_SPI_NSS)  // P1.13
#define LORA_RESET               (32 + 11)      // P1.11
#define LORA_BUSY                (32 + 10)      // P1.10
#define LORA_SCLK                (PIN_SPI_SCK)  // P1.15
#define LORA_MISO                (PIN_SPI_MISO) // P0.29
#define LORA_MOSI                (PIN_SPI_MOSI) // P0.2
#define LORA_CS                  PIN_SPI_NSS    // P1.13

#define LR11X0_DIO_AS_RF_SWITCH  true
#define LR11X0_DIO3_TCXO_VOLTAGE 1.6

#define LR1110_IRQ_PIN           LORA_DIO_1
#define LR1110_NRESET_PIN        LORA_RESET
#define LR1110_BUSY_PIN          LORA_DIO_2
#define LR1110_SPI_NSS_PIN       LORA_CS
#define LR1110_SPI_SCK_PIN       LORA_SCLK
#define LR1110_SPI_MOSI_PIN      LORA_MOSI
#define LR1110_SPI_MISO_PIN      LORA_MISO

// GPS
#define HAS_GPS                  0
#define GPS_RX_PIN               PIN_SERIAL1_RX
#define GPS_TX_PIN               PIN_SERIAL1_TX

#define GPS_EN                   (-1) // P1.11
#define GPS_RESET                (-1) // P1.15

#define GPS_VRTC_EN              (-1) // P0.8
#define GPS_SLEEP_INT            (-1) // P1.12
#define GPS_RTC_INT              (-1) // P0.15
#define GPS_RESETB               (-1) // P1.14
