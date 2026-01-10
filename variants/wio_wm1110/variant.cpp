/*
 * variant.cpp - Seeed Wio WM1110 Dev Board
 * Pin mapping for nRF52840
 */

#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[PINS_COUNT + 1] =
{
  0,  // P0.00
  1,  // P0.01
  2,  // P0.02, AIN0, SENSOR_AIN_0
  3,  // P0.03, AIN1, SENSOR_AIN_1
  4,  // P0.04, AIN2, SENSOR_AIN_2
  5,  // P0.05, AIN3, SENSOR_AIN_3
  6,  // P0.06, PIN_SERIAL2_RX, SENSOR_RXD
  7,  // P0.07, SENSOR_POWER_PIN
  8,  // P0.08, PIN_SERIAL2_TX, SENSOR_TXD
  9,  // P0.09
  10, // P0.10
  11, // P0.11, LIS3DH_INT_PIN_1, SENSOR_INT_1
  12, // P0.12, LIS3DH_INT_PIN_2, SENSOR_INT_2
  13, // P0.13, LED_GREEN, USER_LED_G
  14, // P0.14, LED_RED, USER_LED_R
  15, // P0.15
  16, // P0.16
  17, // P0.17
  18, // P0.18
  19, // P0.19
  20, // P0.20
  21, // P0.21
  22, // P0.22, PIN_SERIAL1_RX, DEBUG_RX_PIN
  23, // P0.23
  24, // P0.24, PIN_SERIAL1_TX, DEBUG_TX_PIN
  25, // P0.25
  26, // P0.26, PIN_WIRE_SCL, SENSOR_SCL
  27, // P0.27, PIN_WIRE_SDA, SENSOR_SDA
  28, // P0.28, AIN4, SENSOR_AIN_4
  29, // P0.29, AIN5, SENSOR_AIN_5
  30, // P0.30, AIN6, SENSOR_AIN_6
  31, // P0.31, AIN7, SENSOR_AIN_7, BATTERY_PIN
  32, // P1.00
  33, // P1.01
  34, // P1.02
  35, // P1.03
  36, // P1.04
  37, // P1.05, LR1110_GNSS_ANT_PIN
  38, // P1.06
  39, // P1.07
  40, // P1.08, LORA_DIO_1, LR1110_IRQ_PIN
  41, // P1.09
  42, // P1.10, LORA_RESET, LR1110_NRESET_PIN
  43, // P1.11, LORA_BUSY, LR1110_BUSY_PIN
  44, // P1.12, PIN_SPI_NSS, LR1110_SPI_NSS_PIN
  45, // P1.13, PIN_SPI_SCK, LR1110_SPI_SCK_PIN
  46, // P1.14, PIN_SPI_MOSI, LR1110_SPI_MOSI_PIN
  47, // P1.15, PIN_SPI_MISO, LR1110_SPI_MISO_PIN
  255,  // NRFX_SPIM_PIN_NOT_USED
};

void initVariant()
{
  // All pins output HIGH by default.
  // https://github.com/Seeed-Studio/Adafruit_nRF52_Arduino/blob/fab7d30a997a1dfeef9d1d59bfb549adda73815a/cores/nRF5/wiring.c#L65-L69

  // Set analog input pins
  pinMode(BATTERY_PIN, INPUT);
  pinMode(SENSOR_AIN_0, INPUT);
  pinMode(SENSOR_AIN_1, INPUT);
  pinMode(SENSOR_AIN_2, INPUT);
  pinMode(SENSOR_AIN_3, INPUT);
  pinMode(SENSOR_AIN_4, INPUT);
  pinMode(SENSOR_AIN_5, INPUT);
  pinMode(SENSOR_AIN_6, INPUT);
  
  // Sensor interrupts as inputs
  pinMode(LIS3DH_INT_PIN_1, INPUT);
  pinMode(LIS3DH_INT_PIN_2, INPUT);

  // Set output pins
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(SENSOR_POWER_PIN, OUTPUT);

  // Initialize outputs to safe states
  digitalWrite(LED_GREEN, HIGH);  // Power indicator LED on
  digitalWrite(LED_RED, LOW);
  digitalWrite(SENSOR_POWER_PIN, LOW);  // Sensors powered off initially
}

