#pragma once

#include "WVariant.h"

#define USE_LFXO
#define VARIANT_MCK 64000000ul
#define PINS_COUNT 48
#define NUM_DIGITAL_PINS 48
#define NUM_ANALOG_INPUTS 0
#define NUM_ANALOG_OUTPUTS 0
#define PIN_LED1 (-1)
#define LED_BUILTIN PIN_LED1
#define LED_BLUE (-1)
#define LED_GREEN (-1)
#define LED_STATE_ON 1
#define PIN_USER_BTN 43
#define PIN_BUTTON2 42

#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA 13
#define PIN_WIRE_SCL 5
#define PIN_CW_INT 12
#define PIN_IO_CSA 20
#define PIN_9006_IO1 4
#define PIN_9006_IO2 41
#define PIN_SERIAL2_RX 26
#define PIN_SERIAL2_TX 15

// EnvironmentSensorManager names the UART pins from the GPS module's side.
#define PIN_GPS_TX 34
#define PIN_GPS_RX 36
#define PIN_SERIAL1_RX PIN_GPS_TX
#define PIN_SERIAL1_TX PIN_GPS_RX
#define PIN_GPS_EN 22
#define PIN_GPS_EN_ACTIVE LOW
#define PIN_GPS_RESET 38
#define PIN_GPS_RESET_ACTIVE LOW
#define PIN_GPS_PPS 10
#define GPS_BAUD_RATE 115200

#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_MISO 2
#define PIN_SPI_MOSI 47
#define PIN_SPI_SCK 45
#define P_LORA_MISO PIN_SPI_MISO
#define P_LORA_MOSI PIN_SPI_MOSI
#define P_LORA_SCLK PIN_SPI_SCK
#define P_LORA_NSS 14
#define P_LORA_DIO_1 31
#define P_LORA_BUSY 29
#define P_LORA_RESET 11
#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f

#define PIN_SPI1_MISO 3
#define PIN_SPI1_MOSI 39
#define PIN_SPI1_SCK 33
#define PIN_TFT_SDA PIN_SPI1_MOSI
#define PIN_TFT_SCL PIN_SPI1_SCK
#define PIN_TFT_CS 16
#define PIN_TFT_DC 32
#define PIN_TFT_RST 24
#define PIN_TFT_EN 9
#define PIN_TFT_EN_ACTIVE LOW
#define PIN_TFT_BL 25
#define PIN_TFT_BL_ACTIVE HIGH
#define SPI_FREQUENCY 4000000
#define DISPLAY_ROTATION 3  // NV3001B MADCTL MX | MV
