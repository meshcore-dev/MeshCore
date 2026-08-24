/*
 * variant.cpp - MuziWorks Base Duo
 * Pin mapping for nRF52840
 */

#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[PINS_COUNT + 1] =
{
  0,  // P0.00
  1,  // P0.01
  2,  // P0.02
  3,  // P0.03
  4,  // P0.04
  5,  // P0.05
  6,  // P0.06
  7,  // P0.07
  8,  // P0.08
  9,  // P0.09
  10, // P0.10
  11, // P0.11
  12, // P0.12
  13, // P0.13
  14, // P0.14
  15, // P0.15
  16, // P0.16
  17, // P0.17
  18, // P0.18
  19, // P0.19
  20, // P0.20
  21, // P0.21
  22, // P0.22
  23, // P0.23
  24, // P0.24, PIN_WIRE_SDA
  25, // P0.25, PIN_WIRE_SCL
  26, // P0.26
  27, // P0.27
  28, // P0.28
  29, // P0.29
  30, // P0.30
  31, // P0.31, BATTERY_PIN (AIN7)
  32, // P1.00
  33, // P1.01
  34, // P1.02
  35, // P1.03, LED_GREEN
  36, // P1.04, LED_BLUE
  37, // P1.05
  38, // P1.06
  39, // P1.07
  40, // P1.08, LORA_DIO_1 (LR1121 IRQ)
  41, // P1.09
  42, // P1.10, LORA_RESET (LR1121 NRESET)
  43, // P1.11, LORA_BUSY (LR1121 BUSY)
  44, // P1.12, PIN_SPI_NSS (LR1121 NSS)
  45, // P1.13, PIN_SPI_SCK (LR1121 SCK)
  46, // P1.14, PIN_SPI_MOSI (LR1121 MOSI)
  47, // P1.15, PIN_SPI_MISO (LR1121 MISO)
  255,  // NRFX_SPIM_PIN_NOT_USED
};

void initVariant()
{
  pinMode(BATTERY_PIN, INPUT);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // LEDs off (active LOW)
  digitalWrite(LED_GREEN, !LED_STATE_ON);
  digitalWrite(LED_BLUE, !LED_STATE_ON);
}
