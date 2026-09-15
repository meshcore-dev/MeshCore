 /*
  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.
  Copyright (c) 2018, Adafruit Industries (adafruit.com)

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _VARIANT_T_IMPULSE_PLUS_NRF52840_
#define _VARIANT_T_IMPULSE_PLUS_NRF52840_

/** Master clock frequency */
#define VARIANT_MCK       (64000000ul)

#define USE_LFXO      // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

#define _PINNUM(port, pin) ((port) * 32 + (pin))

// IIC
#define IIC_SDA_1 _PINNUM(1, 8)
#define IIC_SCL_1 _PINNUM(0, 11)
#define IIC_SDA_2 _PINNUM(0, 20)
#define IIC_SCL_2 _PINNUM(0, 15)

// TTP223
#define TTP223_KEY _PINNUM(1, 4)

// SSD1315
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C
#define SCREEN_RST -1
#define SCREEN_SDA IIC_SDA_2
#define SCREEN_SCL IIC_SCL_2

// Lora S62F(SX1262)
#define SX1262_CS _PINNUM(1, 14)
#define SX1262_RST _PINNUM(0, 2)
#define SX1262_SCLK _PINNUM(0, 3)
#define SX1262_MOSI _PINNUM(0, 28)
#define SX1262_MISO _PINNUM(0, 30)
#define SX1262_BUSY _PINNUM(0, 31)
#define SX1262_INT _PINNUM(0, 29)
#define SX1262_DIO1 _PINNUM(0, 29)
#define SX1262_DIO2 _PINNUM(1, 15)
#define SX1262_RF_VC1 _PINNUM(1, 13)
#define SX1262_RF_VC2 _PINNUM(1, 7)

#define SX126X_TXEN SX1262_RF_VC1
#define SX126X_RXEN SX1262_RF_VC2

#define P_LORA_NSS SX1262_CS
#define P_LORA_DIO_1 SX1262_DIO1
#define P_LORA_RESET SX1262_RST
#define P_LORA_BUSY SX1262_BUSY
#define P_LORA_DIO_0 RADIOLIB_NC
#define P_LORA_DIO_2 SX1262_DIO2
#define P_LORA_SCLK SX1262_SCLK
#define P_LORA_MISO SX1262_MISO
#define P_LORA_MOSI SX1262_MOSI

// RT9080
#define RT9080_EN _PINNUM(0, 14)

// GPS
#define GPS_EN _PINNUM(1, 10)
#define GPS_UART_RX _PINNUM(1, 11)
#define GPS_UART_TX _PINNUM(1, 12)
#define PIN_GPS_RX GPS_UART_RX
#define PIN_GPS_TX GPS_UART_TX
#define PIN_GPS_EN GPS_EN
#define GPS_BAUD_RATE 38400
#define PIN_GPS_EN_ACTIVE LOW

// Battery
#define BATTERY_MEASUREMENT_CONTROL _PINNUM(0, 25)
#define BATTERY_ADC_DATA _PINNUM(0, 5)

// Number of pins defined in PinDescription array
#define PINS_COUNT           (48)
#define NUM_DIGITAL_PINS     (48)
#define NUM_ANALOG_INPUTS    (6)
#define NUM_ANALOG_OUTPUTS   (0)

// LEDs
#define PIN_LED1             (_PINNUM(0, 17))
// #define PIN_LED2             (_PINNUM(1, 5))
// #define PIN_LED3             (15)
// #define PIN_LED4             (16)

#define LED_BUILTIN          PIN_LED1
// #define LED_CONN             PIN_LED2

// #define LED_RED              PIN_LED1
#define LED_BLUE             PIN_LED1

#define LED_STATE_ON         0         // State when LED is litted

/*
 * Buttons
 */
#define PIN_BUTTON1          _PINNUM(0, 24)
// #define PIN_BUTTON2          12
// #define PIN_BUTTON3          24
// #define PIN_BUTTON4          25

/*
 * Analog pins
 */
#define PIN_A0               (3)
#define PIN_A1               (4)
#define PIN_A2               (28)
#define PIN_A3               (29)
#define PIN_A4               (30)
#define PIN_A5               (31)
#define PIN_A6               (0xff)
#define PIN_A7               (0xff)

static const uint8_t A0  = PIN_A0 ;
static const uint8_t A1  = PIN_A1 ;
static const uint8_t A2  = PIN_A2 ;
static const uint8_t A3  = PIN_A3 ;
static const uint8_t A4  = PIN_A4 ;
static const uint8_t A5  = PIN_A5 ;
static const uint8_t A6  = PIN_A6 ;
static const uint8_t A7  = PIN_A7 ;
#define ADC_RESOLUTION    14

// Other pins
#define PIN_AREF           (2)
#define PIN_NFC1           (9)
#define PIN_NFC2           (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */

// Arduino Header D0, D1
#define PIN_SERIAL1_RX      (33) // P1.01
#define PIN_SERIAL1_TX      (34) // P1.02

// Connected to Jlink CDC
#define PIN_SERIAL2_RX      (8)
#define PIN_SERIAL2_TX      (6)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO         (46)
#define PIN_SPI_MOSI         (45)
#define PIN_SPI_SCK          (47)

static const uint8_t SS   = 44 ;
static const uint8_t MOSI = PIN_SPI_MOSI ;
static const uint8_t MISO = PIN_SPI_MISO ;
static const uint8_t SCK  = PIN_SPI_SCK ;

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 2

#define PIN_WIRE_SDA         (26)
#define PIN_WIRE_SCL         (27)

#define PIN_WIRE1_SDA         (26)
#define PIN_WIRE1_SCL         (27)

// ZD25WQ32CEIGR SPI
#define ZD25WQ32C_CS _PINNUM(0, 12)
#define ZD25WQ32C_SCLK _PINNUM(0, 4)
#define ZD25WQ32C_MOSI _PINNUM(0, 6)
#define ZD25WQ32C_MISO _PINNUM(1, 9)
#define ZD25WQ32C_IO0 _PINNUM(0, 6)
#define ZD25WQ32C_IO1 _PINNUM(1, 9)
#define ZD25WQ32C_IO2 _PINNUM(0, 8)
#define ZD25WQ32C_IO3 _PINNUM(0, 26)

#define PIN_QSPI_SCK         ZD25WQ32C_SCLK
#define PIN_QSPI_CS          ZD25WQ32C_CS
#define PIN_QSPI_IO0         ZD25WQ32C_IO0
#define PIN_QSPI_IO1         ZD25WQ32C_IO1
#define PIN_QSPI_IO2         ZD25WQ32C_IO2
#define PIN_QSPI_IO3         ZD25WQ32C_IO3

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES   MX25R6435F
#define EXTERNAL_FLASH_USE_QSPI

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif