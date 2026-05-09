/*
 * variant.h
 *
 * MIT License
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define USE_LFXO    // 32.768 kHz crystal oscillator
#define VARIANT_MCK (64000000ul)

#define WIRE_INTERFACES_COUNT   (1)
////////////////////////////////////////////////////////////////////////////////
// Power

#define PIN_PWR_EN              (30)  // RT9080 LDO enable pin for 3v3 rail

#define PIN_BAT_CTL             (31)  // vdiv enable
#define PIN_VBAT_READ           (2)
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

#define PIN_SERIAL1_RX          PIN_GPS_TX
#define PIN_SERIAL1_TX          PIN_GPS_RX
////////////////////////////////////////////////////////////////////////////////
// I2C pin definition

#define PIN_WIRE_SDA            (36)  // P1.04
#define PIN_WIRE_SCL            (34)  // P1.02
#define I2C_NO_RESCAN
#define DISABLE_DS3231_PROBE    // DS3231 lives at 0x68 but this board has ICM20948 at that address, causing broken clock.

////////////////////////////////////////////////////////////////////////////////
// SPI pin definition

#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (17)
#define PIN_SPI_MOSI            (15)
#define PIN_SPI_SCK             (13)

////////////////////////////////////////////////////////////////////////////////
// QSPI FLASH

#define PIN_QSPI_SCK            (4)
#define PIN_QSPI_CS             (12)
#define PIN_QSPI_IO0            (6)
#define PIN_QSPI_IO1            (8)
#define PIN_QSPI_IO2            (41)  // P1.09
#define PIN_QSPI_IO3            (26)

#define EXTERNAL_FLASH_DEVICES ZD25WQ32CEIGR
#define EXTERNAL_FLASH_USE_QSPI

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs (only WS2812, no traditional LEDs available)

// WS1812 data lines
#define WS2812_DATA_1           (39)  // P1.07
#define WS2812_DATA_2           (44)  // P1.12
#define WS2812_DATA_3           (28)  // P0.28

#define LED_BLUE                (-1)
#define LED_BUILTIN             (-1)
#define LED_PIN                 LED_BUILTIN
#define LED_STATE_ON            LOW

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (42)  // P1.10
#define BUTTON_PIN              PIN_BUTTON1 // BUTTON A
#define PIN_USER_BTN            BUTTON_PIN

#define PIN_BUTTON2             (24)
#define BUTTON_PIN2             PIN_BUTTON2 // BUTTON C

////////////////////////////////////////////////////////////////////////////////
// Lora (Acsip S62F) 

#define  USE_SX1262
#define  P_LORA_SCLK               PIN_SPI_SCK
#define  P_LORA_MISO               PIN_SPI_MISO
#define  P_LORA_MOSI               PIN_SPI_MOSI
#define  P_LORA_DIO_1              (40)  // P1.08
#define  P_LORA_RESET              (7)   // P0.07
#define  P_LORA_BUSY               (14)  // P0.14
#define  P_LORA_NSS                (11)  // P0.11
#define  SX126X_RXEN               (33)  // P1.01
#define  SX126X_TXEN               (27)  // P0.27
#define  SX126X_DIO3_TCXO_VOLTAGE  (1.8f)


////////////////////////////////////////////////////////////////////////////////
// GPS

// NOTE: these pins are defined differently to how lilygo does them but they
// seem to work properly for how EnvironmentSensorManager operates.
// TODO: MAYBE? migrate to board based sensor manager / add GPS_WAKE_UP to ESM

#define PIN_GPS_RX              (21) // GPS_UART_RX in lilygo pin defs
#define PIN_GPS_TX              (19) // GPS_UART_TX in lilygo pin defs
#define PIN_GPS_EN              (25) // GPS_WAKE_UP in lilygo pin defs
#define PIN_GPS_RESET           (47) // GPS_EN      in lilygo pin defs
#define PIN_GPS_STANDBY         (29) // GPS_RF_EN   in lilygo pin defs
#define PIN_GPS_PPS             (23) // GPS_1PPS    in lilygo pin defs

// buzzer - enabled in platformio.ini so it can be easily turned off if not wanted.
// #define PIN_BUZZER              (38) // P1.06

// microphone
#define MICROPHONE_SCLK         (35) // P1.03
#define MICROPHONE_DATA         (37) // P1.05

// speaker
#define SPEAKER_EN              (43) // P1.11
#define SPEAKER_EN_2            (3)  // P0.03
#define SPEAKER_BCLK            (16) // P0.16
#define SPEAKER_DATA            (20) // P0.20
#define SPEAKER_WS_LRCK         (22) // P0.22

// ICM20948 9dof motion sensor (accelerometer and magnetometer)
#define ICM20948_SDA PIN_WIRE_SDA    // P1.4
#define ICM20948_SCL PIN_WIRE_SCL    // P1.2
#define ICM20948_ADDRESS 0x68
