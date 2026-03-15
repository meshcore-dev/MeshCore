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

#ifndef _VARIANT_R1NEO_
#define _VARIANT_R1NEO_

#define RAK4630

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

/* Number of pins defined in PinDescription array */
#define PINS_COUNT         (48)
#define NUM_DIGITAL_PINS   (48)
#define NUM_ANALOG_INPUTS   (8)
#define NUM_ANALOG_OUTPUTS  (0)

/* R1Neo peculiarities */
#define PIN_DCDC_EN_MCU_HOLD (13) // P0.13 (04) DCDC_EN_MCU_HOLD
#define PIN_SOFT_SHUTDOWN    (29) // P0.29 (32) SOFT_SHUTDOWN_SIGNAL
#define PIN_MCU_SIGNAL       (30) // P0.30 (33) MCU_SIGNAL

/* R1Neo LoRa Radio */
// RAK4630/4631 pins

#define  P_LORA_DIO_1    (47)        // P1.15 (SX)
#define  P_LORA_NSS      (42)        // P1.10 (SX)
#define  P_LORA_RESET    RADIOLIB_NC // P1.06 (SX) -- 38
#define  P_LORA_BUSY     (46)        // P1.14 (SX)
#define  P_LORA_SCLK     (43)        // P1.11 (SX)
#define  P_LORA_MISO     (45)        // P1.13 (SX)
#define  P_LORA_MOSI     (44)        // P1.12 (SX)
#define  SX126X_POWER_EN (37)        // P1.05 (SX)

#define SX126X_DIO2_AS_RF_SWITCH  true
#define SX126X_DIO3_TCXO_VOLTAGE   1.8

/* R1Neo peripherals */
/* GPS */
#define GPS_RX      (24) // P0.24 (23) UART_GPS_RX
#define GPS_TX      (25) // P0.25 (24) UART_GPS_TX
#define GPS_EN      (33) // P1.01 (25) GPS_EN
#define GPS_PPS      (2) // P0.02 (30) GPS_PPS

#define PIN_GPS_1PPS  GPS_PPS
#define GPS_BAUD_RATE 9600

/* RTC */
#define RTC_SDA     (19) // P0.19  (9) RTC_SDA
#define RTC_SCL     (20) // P0.20 (10) RTC_SCL

/* LEDs */
#define LED_GREEN   (36) // P1.04 (28) GRN_LED_RAK
#define LED_BLUE    (28) // P0.28 (31) BLU_LED_RAK

#define LED_BUILTIN (0xFF)

#ifndef P_LORA_TX_LED
   #define P_LORA_TX_LED LED_GREEN
#endif

#define LED_STATE_ON 1 // State when LED is lit

/* Buttons */
#define PIN_USER_BTN (26)

/* Buzzer */
#define PIN_BUZZER    (3)

/* Analog pins */
// Arduino makes me angry
#define PIN_A0 (0xFF)  // NOT_PRESENT
#define PIN_A1 (0xFF)  // NOT_PRESENT
#define PIN_A2    (4)  // P0.04 (41) NC
#define PIN_A3    (5)  // P0.05 (40) NC
#define PIN_A4 (0xFF)  // NOT_PRESENT
#define PIN_A5 (0xFF)  // NOT_PRESENT
#define PIN_A6 (0xFF)  // NOT_PRESENT
#define PIN_A7   (31)  // P0.31 (39) ADC_VBAT

   static const uint8_t A0 = PIN_A0;
   static const uint8_t A1 = PIN_A1;
   static const uint8_t A2 = PIN_A2;
   static const uint8_t A3 = PIN_A3;
   static const uint8_t A4 = PIN_A4;
   static const uint8_t A5 = PIN_A5;
   static const uint8_t A6 = PIN_A6;
   static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Other pins
#define PIN_AREF (0xFF) // No analog reference

   static const uint8_t AREF = PIN_AREF;

/* Serial interfaces */
#define PIN_GPS_TX (GPS_TX)
#define PIN_GPS_RX (GPS_RX)
#define PIN_GPS_EN (GPS_EN)

#define PIN_SERIAL1_TX (PIN_GPS_TX)
#define PIN_SERIAL1_RX (PIN_GPS_RX)

/* SPI Interfaces */
// unused pins - define anyways
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MOSI  (9) // P0.09 (13) NC  
#define PIN_SPI_MISO (10) // P0.10 (12) NC
#define PIN_SPI_SCK  (21) // P0.21 (11) NC

/* I2C Interfaces */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (RTC_SDA)
#define PIN_WIRE_SCL (RTC_SCL)

/* QSPI Pins */
// interface occupied by peripherals, define anyways
#define PIN_QSPI_SCK  (3) // P0.03 (29) BUZZER
#define PIN_QSPI_CS  (26) // P0.26 (34) USER_BUTTON
#define PIN_QSPI_IO0 (30) // P0.30 (33) MCU_SIGNAL
#define PIN_QSPI_IO1 (29) // P0.29 (32) SOFT_SHUTDOWN
#define PIN_QSPI_IO2 (28) // P0.28 (31) BLU_LED_RAK
#define PIN_QSPI_IO3  (2) // P0.02 (30) GPS_PPS

/* On-board QSPI Flash */
// No QSPI (define anyways)
#define EXTERNAL_FLASH_DEVICES IS25LP080D
#define EXTERNAL_FLASH_USE_QSPI

/* Battery */
#define PIN_VBAT_READ (31) // P0.31 (39) ADC_VBAT
#define PIN_BAT_CHG   (34) // P1.02 (26) BAT_CHG_STATUS

#define ADC_MULTIPLIER (3 * 1.73 * 1.187 * 1000)

// Power management boot protection threshold (millivolts)
// Set to 0 to disable boot protection
// disabled for now until I can figure this out
#define PWRMGT_VOLTAGE_BOOTLOCK 0   // Won't boot below this voltage (mV)
// LPCOMP wake configuration (voltage recovery from SYSTEMOFF)
// AIN3 = P0.05 = PIN_A0 / PIN_VBAT_READ
#define PWRMGT_LPCOMP_AIN    5
#define PWRMGT_LPCOMP_REFSEL 4  // 5/8 VDD (~3.13-3.44V)

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
