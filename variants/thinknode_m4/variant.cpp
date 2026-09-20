/*
 * variant.cpp
 * Copyright (C) 2023 Seeed K.K.
 * MIT License
 */

#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] =
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
  24, // P0.24
  25, // P0.25
  26, // P0.26
  27, // P0.27
  28, // P0.28
  29, // P0.29
  30, // P0.30
  31, // P0.31
  32, // P1.00
  33, // P1.01
  34, // P1.02
  35, // P1.03
  36, // P1.04
  37, // P1.05
  38, // P1.06
  39, // P1.07
  40, // P1.08
  41, // P1.09
  42, // P1.10
  43, // P1.11
  44, // P1.12
  45, // P1.13
  46, // P1.14
  47, // P1.15
};

void initVariant()
{
/* TODO */
  pinMode(PIN_PWR_EN, OUTPUT);
  pinMode(I2C_POWER, OUTPUT);

  digitalWrite(PIN_PWR_EN, HIGH);
  digitalWrite(I2C_POWER, I2C_POWER_ACTIVE);

  pinMode(LED_STATUS, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_BAT1, OUTPUT);
  pinMode(LED_BAT2, OUTPUT);
  pinMode(LED_BAT3, OUTPUT);
  pinMode(LED_BAT4, OUTPUT);

  digitalWrite(LED_BAT1, LOW);
  digitalWrite(LED_BAT2, LOW);
  digitalWrite(LED_BAT3, LOW);
  digitalWrite(LED_BAT4, LOW);
  digitalWrite(LED_STATUS, LOW);
  digitalWrite(LED_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(PIN_GPS_POWER, OUTPUT);
  pinMode(PIN_GPS_EN, OUTPUT);

  // Power on gps but in standby
  digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
  digitalWrite(PIN_GPS_POWER, GPS_POWER_ACTIVE);
}
