#pragma once

#include "../../mocks/Arduino.h"

#define NRF52_POWER_MANAGEMENT 1
#define PWRMGT_LPCOMP_AIN 3
#define PWRMGT_LPCOMP_REFSEL 4
#define PWRMGT_VOLTAGE_BOOTLOCK 3300
#define SX126X_POWER_EN 21
#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define HIGH 1

void pinMode(uint32_t pin, uint32_t mode);
void digitalWrite(uint32_t pin, uint32_t value);
void analogReadResolution(uint32_t bits);
uint32_t analogRead(uint32_t pin);
