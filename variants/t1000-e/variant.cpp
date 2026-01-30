/*
 * variant.cpp
 * Copyright (C) 2023 Seeed K.K.
 * MIT License
 */

#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[PINS_COUNT + 1] =
{
  0,  // P0.00
  1,  // P0.01
  2,  // P0.02, AIN0 BATTERY_PIN
  3,  // P0.03
  4,  // P0.04, SENSOR_EN
  5,  // P0.05, EXT_PWR_DETEC
  6,  // P0.06, PIN_BUTTON1
  7,  // P0.07, LORA_BUSY
  8,  // P0.08, GPS_VRTC_EN
  9,  // P0.09
  10, // P0.10
  11, // P0.11, PIN_SPI_SCK
  12, // P0.12, PIN_SPI_NSS
  13, // P0.13, PIN_SERIAL1_TX
  14, // P0.14, PIN_SERIAL1_RX
  15, // P0.15, GPS_RTC_INT
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
  26, // P0.26, PIN_WIRE_SDA
  27, // P0.27, PIN_WIRE_SCL
  28, // P0.28
  29, // P0.29, AIN5, LUX_SENSOR
  30, // P0.30
  31, // P0.31, AIN7, TEMP_SENSOR
  32, // P1.00
  33, // P1.01, LORA_DIO_1
  34, // P1.02
  35, // P1.03, EXT_CHRG_DETECT
  36, // P1.04
  37, // P1.05, LR1110_EN
  38, // P1.06, 3V3_EN PWR TO SENSORS
  39, // P1.07, PIN_3V3_ACC_EN
  40, // P1.08, PIN_SPI_MISO
  41, // P1.09, PIN_SPI_MOSI
  42, // P1.10, LORA_RESET
  43, // P1.11, GPS_EN
  44, // P1.12, GPS_SLEEP_INT
  45, // P1.13
  46, // P1.14, GPS_RESETB
  47, // P1.15, PIN_GPS_RESET
  255,  // NRFX_SPIM_PIN_NOT_USED
};

void initVariant()
{
  // All pins output HIGH by default.
  // https://github.com/Seeed-Studio/Adafruit_nRF52_Arduino/blob/fab7d30a997a1dfeef9d1d59bfb549adda73815a/cores/nRF5/wiring.c#L65-L69

  pinMode(BATTERY_PIN, INPUT);
  pinMode(TEMP_SENSOR, INPUT);
  pinMode(LUX_SENSOR, INPUT);
  pinMode(EXT_CHRG_DETECT, INPUT);
  pinMode(EXT_PWR_DETECT, INPUT);
  pinMode(GPS_RESETB, INPUT);
  pinMode(PIN_BUTTON1, INPUT);

  pinMode(PIN_3V3_EN, OUTPUT);
  pinMode(PIN_3V3_ACC_EN, OUTPUT);
  pinMode(BUZZER_EN, OUTPUT);
  pinMode(SENSOR_EN, OUTPUT);
  pinMode(GPS_EN, OUTPUT);
  pinMode(GPS_RESET, OUTPUT);
  pinMode(GPS_VRTC_EN, OUTPUT);
  pinMode(GPS_SLEEP_INT, OUTPUT);
  pinMode(GPS_RTC_INT, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(PIN_3V3_EN, LOW);
  digitalWrite(PIN_3V3_ACC_EN, LOW);
  digitalWrite(BUZZER_EN, LOW);
  digitalWrite(SENSOR_EN, LOW);
  digitalWrite(GPS_EN, LOW);
  digitalWrite(GPS_RESET, LOW);
  digitalWrite(GPS_VRTC_EN, LOW);
  digitalWrite(GPS_SLEEP_INT, HIGH);
  digitalWrite(GPS_RTC_INT, LOW);
  digitalWrite(LED_PIN, LOW);
}
