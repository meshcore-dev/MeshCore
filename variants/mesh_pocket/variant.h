/*
 * variant.h
 * MIT License
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define USE_LFXO    // 32.768 kHz crystal oscillator
#define VARIANT_MCK (64000000ul)


////////////////////////////////////////////////////////////////////////////////
// Power

#define BATTERY_PIN             (0 + 29)
#define PIN_BAT_CTRL            (32 + 2)
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

#define PIN_SERIAL1_RX          (37)
#define PIN_SERIAL1_TX          (39)

#define PIN_SERIAL2_RX          (7)
#define PIN_SERIAL2_TX          (8)

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition
#define WIRE_INTERFACES_COUNT 	(1)

#define PIN_WIRE_SDA (32+15)
#define PIN_WIRE_SCL (32+13)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_BUILTIN             (13)
#define PIN_LED                 LED_BUILTIN
#define LED_RED                 LED_BUILTIN
#define LED_BLUE                (-1)            // No blue led, prevents Bluefruit flashing the green LED during advertising
#define PIN_STATUS_LED          LED_BUILTIN

#define LED_STATE_ON            LOW

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (32 + 10)
#define BUTTON_PIN              PIN_BUTTON1

// #define PIN_BUTTON2             (0 + 18)
// #define BUTTON_PIN2             PIN_BUTTON2

#define PIN_USER_BTN            BUTTON_PIN

////////////////////////////////////////////////////////////////////////////////

// SPI pin definition
#define SPI_INTERFACES_COUNT    (2)

// Lora
#define USE_SX1262
#define SX126X_CS               (0 + 26)
#define SX126X_DIO1             (0 + 16)
#define SX126X_BUSY             (0 + 15)
#define SX126X_RESET            (0 + 12)
#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define PIN_SPI_MISO            (32 + 9)
#define PIN_SPI_MOSI            (0 + 5)
#define PIN_SPI_SCK             (0 + 4)

#define LORA_CS       SX126X_CS
#define P_LORA_DIO_1  SX126X_DIO1
#define P_LORA_NSS    SX126X_CS
#define P_LORA_RESET  SX126X_RESET
#define P_LORA_BUSY   SX126X_BUSY
#define P_LORA_SCLK   PIN_SPI_SCK
#define P_LORA_MISO   PIN_SPI_MISO
#define P_LORA_MOSI   PIN_SPI_MOSI


////////////////////////////////////////////////////////////////////////////////
// EInk

#define PIN_DISPLAY_CS                 (24)
#define PIN_DISPLAY_BUSY               (32 + 6)
#define PIN_DISPLAY_DC                 (31)
#define PIN_DISPLAY_RST                (32 + 4)

#define PIN_SPI1_MISO           (-1)
#define PIN_SPI1_MOSI           (20)
#define PIN_SPI1_SCK            (22)


// GxEPD2 needs that for a panel that is not even used !
extern const int MISO;
extern const int MOSI;
extern const int SCK;


#undef HAS_GPS
#define HAS_GPS 0
#define HAS_RTC 0