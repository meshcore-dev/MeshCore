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

#ifndef _VARIANT_MUZI_BASE_UNO_
#define _VARIANT_MUZI_BASE_UNO_

#define MUZI_BASE_UNO

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

// Number of pins defined in PinDescription array
#define PINS_COUNT           (48)
#define NUM_DIGITAL_PINS     (48)
#define NUM_ANALOG_INPUTS    (6)
#define NUM_ANALOG_OUTPUTS   (0)

/*
 * LEDs
 */
#define PIN_LED1             (35)   // P1.03 - Green LED
#define PIN_LED2             (36)   // P1.04 - Blue LED

#define LED_BUILTIN          PIN_LED1
#define LED_GREEN            PIN_LED1
#define LED_BLUE             PIN_LED2
#define LED_STATE_ON         HIGH

/*
 * Buttons
 */
#define PIN_BUTTON1          (9)    // P0.09 - User button

/*
 * VBAT sensing
 */
#define PIN_VBAT_READ                   (5)   // P0.05 / AIN3
#define BATTERY_SENSE_RESOLUTION_BITS   12
#define BATTERY_SENSE_RESOLUTION        4096.0
#define AREF_VOLTAGE                    3.0
#define VBAT_AR_INTERNAL                AR_INTERNAL_3_0
#define ADC_MULTIPLIER                  1.73
#define ADC_RESOLUTION                  14

// Power management boot protection threshold (millivolts)
#define PWRMGT_VOLTAGE_BOOTLOCK  3300   // Won't boot below this voltage (mV)
// LPCOMP wake configuration (voltage recovery from SYSTEMOFF)
// AIN3 = P0.05 = PIN_VBAT_READ
#define PWRMGT_LPCOMP_AIN       3
#define PWRMGT_LPCOMP_REFSEL    4       // 5/8 VDD (~3.13-3.44V)

/*
 * Analog pins
 */
#define PIN_A0               (5)    // P0.05 / AIN3
#define PIN_A1               (31)   // P0.31 / AIN7
#define PIN_A2               (28)   // P0.28 / AIN4
#define PIN_A3               (29)   // P0.29 / AIN5
#define PIN_A4               (30)   // P0.30 / AIN6
#define PIN_A5               (2)    // P0.02 / AIN0
#define PIN_A6               (0xff)
#define PIN_A7               (0xff)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;

// Other pins
#define PIN_AREF             (2)
#define PIN_NFC1             (9)
#define PIN_NFC2             (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
#define PIN_SERIAL1_RX       (15)   // P0.15
#define PIN_SERIAL1_TX       (16)   // P0.16

/*
 * SPI Interfaces
 *
 * PIN_SPI_* defines the default/external SPI available on castellated pads.
 * The LoRa radio uses P_LORA_* SPI pins (internal to the nRFLR1262 module),
 * which are configured by std_init() in the CustomSX1262 driver.
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO         (29)   // P0.29 - External SPI (castellated pad)
#define PIN_SPI_MOSI         (30)   // P0.30 - External SPI (castellated pad)
#define PIN_SPI_SCK          (3)    // P0.03 - External SPI (castellated pad)

static const uint8_t SS   = 26;    // P0.26 - External SPI CS (castellated pad)
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK  = PIN_SPI_SCK;

/*
 * LoRa radio module pins (SX1262 internal to nRFLR1262 module)
 *
 * Pin mapping from Elecrow nRFLR1262 module block diagram:
 *   SX1262 DIO1/IRQ  -> nRF52840 P1.06 (pin 38)
 *   SX1262 NSS/SS    -> nRF52840 P1.12 (pin 44)
 *   SX1262 SCK       -> nRF52840 P1.13 (pin 45)
 *   SX1262 MOSI      -> nRF52840 P1.14 (pin 46)
 *   SX1262 MISO      -> nRF52840 P1.15 (pin 47)
 *   SX1262 BUSY      -> nRF52840 P1.11 (pin 43)
 *   SX1262 NRESET    -> nRF52840 P1.10 (pin 42)
 */
#define P_LORA_DIO_1         (38)   // P1.06 - SX1262 DIO1/IRQ
#define P_LORA_NSS           (44)   // P1.12 - SX1262 NSS
#define P_LORA_SCLK          (45)   // P1.13 - SX1262 SCK
#define P_LORA_MOSI          (46)   // P1.14 - SX1262 MOSI
#define P_LORA_MISO          (47)   // P1.15 - SX1262 MISO
#define P_LORA_BUSY          (43)   // P1.11 - SX1262 BUSY
#define P_LORA_RESET         (42)   // P1.10 - SX1262 NRESET

// SX1262 power enable - P1.05 (pin 37) internal to nRFLR1262 module
#define SX126X_POWER_EN      (37)   // P1.05

#define SX126X_DIO2_AS_RF_SWITCH   true
#define SX126X_DIO3_TCXO_VOLTAGE   (1.8f)

/*
 * Wire Interfaces (I2C)
 */
#define WIRE_INTERFACES_COUNT  1

#define PIN_WIRE_SDA         (13)   // P0.13 - QWIIC/STEMMA QT SDA
#define PIN_WIRE_SCL         (14)   // P0.14 - QWIIC/STEMMA QT SCL

/*
 * QSPI Pins - External 2MB QSPI flash on Base board
 * Uses standard nRF52840 dedicated QSPI pins
 */
#define PIN_QSPI_SCK         (19)   // P0.19
#define PIN_QSPI_CS          (17)   // P0.17
#define PIN_QSPI_IO0         (20)   // P0.20
#define PIN_QSPI_IO1         (21)   // P0.21
#define PIN_QSPI_IO2         (22)   // P0.22
#define PIN_QSPI_IO3         (23)   // P0.23

// On-board 2MB QSPI Flash
#define EXTERNAL_FLASH_DEVICES  IS25LP016D
#define EXTERNAL_FLASH_USE_QSPI

#endif // _VARIANT_MUZI_BASE_UNO_
