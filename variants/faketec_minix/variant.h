/*
 * variant.h
 * Copyright (C) 2023 Seeed K.K.
 * MIT License
 */

 #pragma once

 #include "WVariant.h"
 
 ////////////////////////////////////////////////////////////////////////////////
 // Low frequency clock source 

#define VARIANT_MCK       (64000000ul)

#define USE_LFXO      // 32.768 kHz crystal oscillator
// #define USE_LFRC    // 32.768 kHz RC oscillator

////////////////////////////////////////////////////////////////////////////////
// Power

#define BATTERY_PIN          (17)
#define ADC_RESOLUTION       12

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT           (23)
#define NUM_DIGITAL_PINS     (23)
#define NUM_ANALOG_INPUTS    (3)
#define NUM_ANALOG_OUTPUTS   (0)

////////////////////////////////////////////////////////////////////////////////
// UART pin definition

#define PIN_SERIAL1_TX       (4)
#define PIN_SERIAL1_RX       (3)
#define PIN_GPS_RX           PIN_SERIAL1_RX
#define PIN_GPS_TX           PIN_SERIAL1_TX
#define PIN_GPS_EN           (5)

#define PIN_BUZZER           (0)    // (T2)
#define PIN_VIBRATION        (1)   // (T3)

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA         (8)
#define PIN_WIRE_SCL         (7)

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_SCK          (12)
#define PIN_SPI_MISO         (15)
#define PIN_SPI_MOSI         (14)

#define PIN_SPI_NSS          (13)

#define PIN_SPI1_SCK         -1//(18)
#define PIN_SPI1_MISO        -1//(19)
#define PIN_SPI1_MOSI        -1//(20)

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define PIN_LED              (1)
#define LED_PIN              PIN_LED
#define LED_BLUE             (23)
#define LED_RED             LED_BLUE
#define LED_GREEN           LED_BLUE
#define LED_BUILTIN          PIN_LED
#define LED_STATE_ON         1
#define PIN_STATUS_LED      PIN_LED

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define UI_HAS_JOYSTICK                    1
#define PIN_BUTTON1                        (6)
#define PIN_BACK_BTN                       PIN_BUTTON1
// #define PIN_USER_BTN                       PIN_BUTTON1
#define JOYSTICK_LEFT                      (21)   // encoder left
#define JOYSTICK_RIGHT                     (18)  // encoder right
#define PIN_USER_BTN                       (19)// encoder press
// #define PIN_USER_BTN                       PIN_BUTTON1

////////////////////////////////////////////////////////////////////////////////
// Buzzer & Vibro
#define PIN_BUZZER                         (0)
#define PIN_VIBRATION                      (1)
