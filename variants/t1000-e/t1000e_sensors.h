#pragma once
#include <stdint.h>

// Light and temperature sensors are on ADC ports
// functions adapted from Seeed examples to get values
// see : https://github.com/Seeed-Studio/Seeed-Tracker-T1000-E-for-LoRaWAN-dev-board

extern uint32_t t1000e_get_light();
extern float t1000e_get_temperature();