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
#define PIN_TXCO                (21)
////////////////////////////////////////////////////////////////////////////////
// Power

#define PIN_PWR_EN              (12)

#define BATTERY_PIN             (5)
#define ADC_MULTIPLIER          (1.73F)

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

#define PIN_SERIAL1_RX          (15)
#define PIN_SERIAL1_TX          (16)

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition
#define WIRE_INTERFACES_COUNT   (1)
#define PIN_WIRE_SDA            (25)
#define PIN_WIRE_SCL            (24)

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (2)

#define PIN_SPI_MISO            (45)
#define PIN_SPI_MOSI            (44)
#define PIN_SPI_SCK             (43)
#define PIN_SPI_NSS             (42)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define LED_RED                 (-1)
#define LED_BLUE                (36)
#define LED_GREEN               (35)

#define PIN_STATUS_LED          LED_BLUE
#define LED_BUILTIN             LED_GREEN
#define LED_PIN                 LED_GREEN
#define LED_STATE_ON            HIGH

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (9)
#define BUTTON_PIN              PIN_BUTTON1
#define PIN_USER_BTN            BUTTON_PIN

#define PIN_BUTTON2             (12)
#define BUTTON_PIN2             PIN_BUTTON2

////////////////////////////////////////////////////////////////////////////////
// Lora

#define USE_SX1262
#define LORA_CS                 (42)
#define SX126X_DIO1             (47)
#define SX126X_BUSY             (46)
#define SX126X_RESET            (38)
#define SX126X_POWER_EN         (37)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

////////////////////////////////////////////////////////////////////////////////
// SPI1

#define PIN_SPI1_MISO           (29)
#define PIN_SPI1_MOSI           (30)
#define PIN_SPI1_SCK            (3)

// GxEPD2 needs that for a panel that is not even used !
extern const int MISO;
extern const int MOSI;
extern const int SCK;

////////////////////////////////////////////////////////////////////////////////
// GPS

#define PIN_GPS_RX              (PIN_SERIAL1_TX)
#define PIN_GPS_TX              (PIN_SERIAL1_RX)
#define PIN_GPS_PPS             (17)
#define PIN_GPS_EN              (34)

///////////////////////////////////////////////////////////////////////////////
// OTHER PINS
#define PIN_AREF                (2)
#define PIN_NFC1                (9)
#define PIN_NFC2                (10)
#define PIN_BUZZER              (21)
