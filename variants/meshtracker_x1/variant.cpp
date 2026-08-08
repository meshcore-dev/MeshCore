/*
 * variant.cpp - Seeed SenseCAP MeshTracker X1 (nRF52840 + Semtech LR2021)
 */

#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[PINS_COUNT + 1] =
{
  0,  // P0.00
  1,  // P0.01
  2,  // P0.02, AIN0 BATTERY_PIN
  3,  // P0.03, LED_RED
  4,  // P0.04, AIN2 VCC_ADC
  5,  // P0.05, AIN3 EXT_PWR_DETECT
  6,  // P0.06, PIN_BUTTON1
  7,  // P0.07, LORA_BUSY
  8,  // P0.08, GPS_RESET
  9,  // P0.09, PIN_RTC_EN
  10, // P0.10
  11, // P0.11, PIN_SPI_SCK
  12, // P0.12, PIN_SPI_NSS
  13, // P0.13, PIN_SERIAL1_TX
  14, // P0.14, PIN_SERIAL1_RX
  15, // P0.15
  16, // P0.16, PIN_SERIAL2_TX
  17, // P0.17, PIN_SERIAL2_RX
  18, // P0.18
  19, // P0.19
  20, // P0.20
  21, // P0.21
  22, // P0.22
  23, // P0.23
  24, // P0.24, LED_GREEN
  25, // P0.25, BUZZER_PIN
  26, // P0.26
  27, // P0.27
  28, // P0.28, LED_BLUE
  29, // P0.29, GPS_RTC_INT
  30, // P0.30, GPS_SLEEP_INT
  31, // P0.31
  32, // P1.00
  33, // P1.01, LORA_DIO_1 (LR2021 DIO8 IRQ)
  34, // P1.02
  35, // P1.03, EXT_CHRG_DETECT
  36, // P1.04, CHARGE_DONE
  37, // P1.05, PIN_DRV_EN
  38, // P1.06, PIN_BAT_ADC_EN
  39, // P1.07, PIN_3V3_EN
  40, // P1.08, PIN_SPI_MISO
  41, // P1.09, PIN_SPI_MOSI
  42, // P1.10, LORA_RESET
  43, // P1.11, GPS_EN
  44, // P1.12
  45, // P1.13, GPS_VRTC_EN
  46, // P1.14, PIN_WIRE_SCL
  47, // P1.15, PIN_WIRE_SDA
  255,  // NRFX_SPIM_PIN_NOT_USED
};

void initVariant()
{
  pinMode(BATTERY_PIN, INPUT);
  pinMode(EXT_CHRG_DETECT, INPUT_PULLUP);
  pinMode(EXT_CHRG_DONE, INPUT_PULLUP);
  pinMode(EXT_PWR_DETECT, INPUT);
  pinMode(PIN_BUTTON1, INPUT_PULLDOWN);

  pinMode(PIN_3V3_EN, OUTPUT);
  digitalWrite(PIN_3V3_EN, HIGH);

  pinMode(PIN_BAT_ADC_EN, OUTPUT);
  digitalWrite(PIN_BAT_ADC_EN, HIGH);

  pinMode(PIN_RTC_EN, OUTPUT);
  digitalWrite(PIN_RTC_EN, LOW);

  pinMode(PIN_DRV_EN, OUTPUT);
  digitalWrite(PIN_DRV_EN, LOW);

  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, LOW);

  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, LOW);

  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);

  pinMode(GPS_EN, OUTPUT);
  digitalWrite(GPS_EN, LOW);

  pinMode(GPS_VRTC_EN, OUTPUT);
  digitalWrite(GPS_VRTC_EN, HIGH);

  pinMode(GPS_RESET, OUTPUT);
  digitalWrite(GPS_RESET, LOW);

  pinMode(GPS_SLEEP_INT, OUTPUT);
  digitalWrite(GPS_SLEEP_INT, HIGH);

  pinMode(GPS_RTC_INT, OUTPUT);
  digitalWrite(GPS_RTC_INT, LOW);

  pinMode(BUZZER_PIN, INPUT_PULLDOWN);
}
