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
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    23,
    24,
    25,
    26,
    27,
    28,
    29,
    30,
    31,
// P1 pins.
    32,
    33,
    34,
    35,
    36,
    37,
    38,
    39,
    40,
    41,
    42,
    43,
    44,
    45,
    46,
    47,
};

void initVariant()
{
  // All pins output HIGH by default.
  // https://github.com/Seeed-Studio/Adafruit_nRF52_Arduino/blob/fab7d30a997a1dfeef9d1d59bfb549adda73815a/cores/nRF5/wiring.c#L65-L69

  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(PIN_BATTERY_CHARGING, INPUT);
  pinMode(PIN_CHARGER_FAULT, INPUT);
  pinMode(PIN_BUTTON1, INPUT);
  pinMode(PIN_BUTTON2, INPUT);
  pinMode(PIN_BUTTON3, INPUT);
  pinMode(PIN_BUTTON4, INPUT);
  pinMode(PIN_BUTTON5, INPUT);
  pinMode(PIN_BUTTON6, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_BLUE, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // turn off buzzer at start. don't leave it high.
  pinMode(GPS_EN_GPIO, OUTPUT);
  digitalWrite(GPS_EN_GPIO, HIGH); // needs to turn on for GPS detection

  pinMode(SCREEN_12V_ENABLE, OUTPUT);
  digitalWrite(SCREEN_12V_ENABLE, LOW); // disable 12V power for SH1107 display for now. 
}