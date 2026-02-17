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

#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include "nrf.h"

const uint32_t g_ADigitalPinMap[] =
{
  // P0
  0,  // P0.00 (NC) (XTAL)
  1,  // P0.01 (NC) (XTAL)
  2,  // P0.02 (30) GPS_PPS
  3,  // P0.03 (29) BUZZER_DRIVE
  4,  // P0.04 (41) NC
  5,  // P0.05 (40) NC
  6,  // P0.06 (NC) NOT_PRESENT
  7,  // P0.07 (NC) (TRACECLK)
  8,  // P0.08 (NC) NOT_PRESENT
  9,  // P0.09 (13) NC
  10, // P0.10 (12) NC
  11, // P0.11 (NC) NOT_PRESENT
  12, // P0.12 (NC) NOT_PRESENT
  13, // P0.13 (04) DCDC_EN_MCU_HOLD
  14, // P0.14 (05) NC
  15, // P0.15 (06) NC
  16, // P0.16 (07) NC
  17, // P0.17 (08) NC
  18, // P0.18 (17) !RESET
  19, // P0.19 (09) RTC_SDA
  20, // P0.20 (10) RTC_SCL
  21, // P0.21 (11) NC
  22, // P0.22 (NC) NOT_PRESENT
  23, // P0.23 (NC) NOT_PRESENT
  24, // P0.24 (23) UART_GPS_RX
  25, // P0.25 (24) UART_GPS_TX
  26, // P0.26 (26) BTN_OK/USR_BTN_PROCESSED
  27, // P0.27 (NC) NOT_PRESENT
  28, // P0.28 (31) BLU_LED_RAK
  29, // P0.29 (32) SOFT_SHUTDOWN_SIGNAL
  30, // P0.30 (33) MCU_SIGNAL
  31, // P0.31 (39) ADC_VBAT

  // P1
  32, // P1.00 (NC) NOT_PRESENT
  33, // P1.01 (25) GPS_EN
  34, // P1.02 (26) BAT_CHG_STATUS
  35, // P1.03 (27) NC
  36, // P1.04 (28) GRN_LED_RAK
  37, // P1.05 (SX) SX126X_POWER_EN
  38, // P1.06 (SX) P_LORA_RESET
  39, // P1.07 (NC) NOT_PRESENT
  40, // P1.08 (NC) NOT_PRESENT
  41, // P1.09 (NC) NOT_PRESENT
  42, // P1.10 (SX) P_LORA_NSS
  43, // P1.11 (SX) P_LORA_SCLK
  44, // P1.12 (SX) P_LORA_MOSI
  45, // P1.13 (SX) P_LORA_MISO
  46, // P1.14 (SX) P_LORA_BUSY
  47  // P1.15 (SX) P_LORA_DIO_1
};


void initVariant()
{
  // Red & Green LEDs - enable & turn off
  pinMode(LED_GREEN, OUTPUT);
  ledOff(LED_GREEN);

  pinMode(LED_BLUE, OUTPUT);
  ledOff(LED_BLUE);

  pinMode(PIN_GPS_EN, OUTPUT);
}
