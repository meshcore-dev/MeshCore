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

#ifndef _VARIANT_RAK4630_
#define _VARIANT_RAK4630_

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

	/*
 * WisBlock Base GPIO definitions
 */
	static const uint8_t WB_IO1 = 17;	   // SLOT_A SLOT_B
	static const uint8_t WB_IO2 = 34;	   // SLOT_A SLOT_B
	static const uint8_t WB_IO3 = 21;	   // SLOT_C
	static const uint8_t WB_IO4 = 4;	   // SLOT_C
	static const uint8_t WB_IO5 = 9;	   // SLOT_D
	static const uint8_t WB_IO6 = 10;	   // SLOT_D
	static const uint8_t WB_SW1 = 33;	   // IO_SLOT
	static const uint8_t WB_A0 = 5;		   // IO_SLOT
	static const uint8_t WB_A1 = 31;	   // IO_SLOT
	static const uint8_t WB_I2C1_SDA = 13; // SENSOR_SLOT IO_SLOT
	static const uint8_t WB_I2C1_SCL = 14; // SENSOR_SLOT IO_SLOT
	static const uint8_t WB_I2C2_SDA = 24; // IO_SLOT
	static const uint8_t WB_I2C2_SCL = 25; // IO_SLOT
	static const uint8_t WB_SPI_CS = 26;   // IO_SLOT
	static const uint8_t WB_SPI_CLK = 3;   // IO_SLOT
	static const uint8_t WB_SPI_MISO = 29; // IO_SLOT
	static const uint8_t WB_SPI_MOSI = 30; // IO_SLOT

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (35)
#define PIN_LED2 (36)

#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2

#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2

#define LED_STATE_ON 1 // State when LED is litted

/*
 * Buttons
 */
// RAK4631 has no buttons

/*
 * Analog pins
 */
#define PIN_A0 (5)	//(3)
#define PIN_A1 (31) //(4)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
#define PIN_A6 (0xff)
#define PIN_A7 (0xff)

	static const uint8_t A0 = PIN_A0;
	static const uint8_t A1 = PIN_A1;
	static const uint8_t A2 = PIN_A2;
	static const uint8_t A3 = PIN_A3;
	static const uint8_t A4 = PIN_A4;
	static const uint8_t A5 = PIN_A5;
	static const uint8_t A6 = PIN_A6;
	static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Power management boot protection threshold (millivolts)
// Set to 0 to disable boot protection
#define PWRMGT_VOLTAGE_BOOTLOCK 3300   // Won't boot below this voltage (mV)
// LPCOMP wake configuration (voltage recovery from SYSTEMOFF)
// AIN3 = P0.05 = PIN_A0 / PIN_VBAT_READ
#define PWRMGT_LPCOMP_AIN 3
#define PWRMGT_LPCOMP_REFSEL 4  // 5/8 VDD (~3.13-3.44V)

// Other pins
#define PIN_AREF (2)
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)

	static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
// TXD1 RXD1 on Base Board
#define PIN_SERIAL1_RX (15)
#define PIN_SERIAL1_TX (16)

// TXD0 RXD0 on Base Board
#define PIN_SERIAL2_RX (19)
#define PIN_SERIAL2_TX (20)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 1

#define PIN_SPI_MISO (29)
#define PIN_SPI_MOSI (30)
#define PIN_SPI_SCK (3)

	static const uint8_t SS = 26;
	static const uint8_t MOSI = PIN_SPI_MOSI;
	static const uint8_t MISO = PIN_SPI_MISO;
	static const uint8_t SCK = PIN_SPI_SCK;

// LoRa radio module pins for RAK4631
#define  P_LORA_DIO_1 (47)
#define  P_LORA_NSS (42)
#define  P_LORA_RESET (-1)
#define  P_LORA_BUSY (46)
#define  P_LORA_SCLK (43)
#define  P_LORA_MISO (45)
#define  P_LORA_MOSI (44)
#define  SX126X_POWER_EN (37)

#define SX126X_DIO2_AS_RF_SWITCH  true
#define SX126X_DIO3_TCXO_VOLTAGE   1.8

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 2

#define PIN_WIRE_SDA (13)
#define PIN_WIRE_SCL (14)

#define PIN_WIRE1_SDA (24)
#define PIN_WIRE1_SCL (25)

// QSPI Pins
// QSPI occupied by GPIO's
#define PIN_QSPI_SCK 3	// 19
#define PIN_QSPI_CS 26	// 17
#define PIN_QSPI_IO0 30 // 20
#define PIN_QSPI_IO1 29 // 21
#define PIN_QSPI_IO2 28 // 22
#define PIN_QSPI_IO3 2	// 23

// On-board QSPI Flash
// No onboard flash
#define EXTERNAL_FLASH_DEVICES IS25LP080D
#define EXTERNAL_FLASH_USE_QSPI

#define PIN_GPS_1PPS      17  //GPS PPS pin
#define GPS_BAUD_RATE   9600
#define GPS_ADDRESS   0x42  //i2c address for GPS

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
