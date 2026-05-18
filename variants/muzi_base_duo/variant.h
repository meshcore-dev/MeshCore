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

#define PIN_VBAT_READ                   (31)   // P0.31
#define BATTERY_SENSE_RESOLUTION_BITS   12
#define BATTERY_SENSE_RESOLUTION        4096.0
#define AREF_VOLTAGE                    3.0
#define VBAT_AR_INTERNAL                AR_INTERNAL_3_0
#define ADC_MULTIPLIER                  1.537
#define ADC_RESOLUTION                  14
#define PIN_BATTERY_CHARGING              (32+2)  // P1.02 STAT2 
#define PIN_CHARGER_FAULT                 (27)   // P0.27 STAT1 this pin is disabled on meshtastic.
// BQ25185 has 2 status pins: STAT1 and STAT2. Both are high when not charging. STAT1 high, STAT2 low: charging. Recoverable fault: STAT1 low, STAT2 high. Unrecoverable fault: both low. 
// We only need to detect charging vs not charging, but someone else can use the fault pin to log when the battery gets too hot or cold.

// Power management boot protection threshold (millivolts)
#define PWRMGT_VOLTAGE_BOOTLOCK  3100   // Won't boot below this voltage (mV). BB15 battery min voltage is 3v, 3100mV is minimum batt in meshtastic code.

// LPCOMP wake configuration (voltage recovery from SYSTEMOFF)
#define PWRMGT_LPCOMP_AIN       7      // AIN7 = P0.31 = PIN_VBAT_READ
#define PWRMGT_LPCOMP_REFSEL    4       // 5/8 VDD (~3.13-3.44V) was the default on RAK4631. should still apply here. 

// Other pins
#define PIN_AREF             (-1)
#define SCREEN_12V_ENABLE   (23) // SH1107 OLED controller has a pin that needs to be enabled to turn on the screen. 

static const uint8_t AREF = (PIN_AREF); // not used 

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

////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define HAS_WIRE                (1)
#define WIRE_INTERFACES_COUNT   (2)

#define PIN_WIRE1_SDA            (4)             // P0.4
#define PIN_WIRE1_SCL            (6)             // P0.6
#define PIN_WIRE_SDA           (24)             // P0.24 OLED I2C
#define PIN_WIRE_SCL           (25)             // P0.25 OLED I2C
#define I2C_NO_RESCAN
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

#define LED_BUILTIN             (35)
#define LED_BLUE                (-1)            // P1.04 turned off, because the blue LED was annoying. 
// #define LED_GREEN               (35)            // P1.03
#define LED_PIN                 LED_BUILTIN

#define LED_STATE_ON            LOW

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons
#define PIN_BUTTON1             (10)             // P0.10 Menu / User Button | on superIO, this is in the center of the "D-Pad", but it's also the button on the Uno/Duo.
#define PIN_BUTTON2             (21) // Joystick Up
#define PIN_BUTTON3             (17) // Joystick Down
#define PIN_BUTTON4             (37) // Joystick Left
#define PIN_BUTTON5             (16) // Joystick Right
#define PIN_BUTTON6             (15) // Back / Cancel Button.
#define JOYSTICK_PRESS          PIN_BUTTON1
#define JOYSTICK_UP             PIN_BUTTON2
#define JOYSTICK_DOWN           PIN_BUTTON3
#define JOYSTICK_LEFT           PIN_BUTTON4
#define JOYSTICK_RIGHT          PIN_BUTTON5
#define PIN_BACK_BTN            PIN_BUTTON6

////////////////////////////////////////////////////////////////////////////////
// LR1121

#define LORA_DIO_1              (32+8)            // P1.08
#define LORA_NSS                (PIN_SPI_NSS)   // P1.12
#define LORA_RESET              (32+10)            // P1.10
#define LORA_BUSY               (32+11)             // P1.11
#define LORA_SCLK               (PIN_SPI_SCK) 
#define LORA_MISO               (PIN_SPI_MISO)
#define LORA_MOSI               (PIN_SPI_MOSI)
 
#define LR11X0_DIO_AS_RF_SWITCH    true
#define LR11X0_DIO3_TCXO_VOLTAGE   3.0

////////////////////////////////////////////////////////////////////////////////
// QSPI Flash
#define PIN_QSPI_SCK (0 + 3)
#define PIN_QSPI_CS (0 + 26)
#define PIN_QSPI_IO0 (0 + 30)
#define PIN_QSPI_IO1 (0 + 29)
#define PIN_QSPI_IO2 (0 + 28)
#define PIN_QSPI_IO3 (0 + 2)

#define EXTERNAL_FLASH_DEVICES W25Q128JVPQ
#define EXTERNAL_FLASH_USE_QSPI

////////////////////////////////////////////////////////////////////////////////
// GPS
#define HAS_GPS                 1
#define PIN_GPS_RX              PIN_SERIAL1_RX
#define PIN_GPS_TX              PIN_SERIAL1_TX
#define GPS_EN_GPIO                  (32+1)            // P1.01 PWR_IO2 on schematic. just cuts power to gps. 

////////////////////////////////////////////////////////////////////////////////
// Buzzer

#define BUZZER_PIN              (22)            // P0.22 same load switch design as GPS_EN. 