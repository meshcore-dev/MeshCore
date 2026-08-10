#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
  8,  // P0.08 = GPIO 0
  6,  // P0.06 = GPIO 1
  17, // P0.17 = GPIO 2
  20, // P0.20 = GPIO 3
  22, // P0.22 = GPIO 4
  24, // P0.24 = GPIO 5
  32, // P1.00 = GPIO 6
  11, // P0.11 = GPIO 7
  36, // P1.04 = GPIO 8
  38, // P1.06 = GPIO 9
  9,  // P0.09 = GPIO 10
  10, // P0.10 = GPIO 11
  43, // P1.11 = GPIO 12
  45, // P1.13 = GPIO 13
  47, // P1.15 = GPIO 14
  2,  // P0.02 = GPIO 15
  29, // P0.29 = GPIO 16
  31, // P0.31 = GPIO 17
  33, // P1.01 = GPIO 18
  34, // P1.02 = GPIO 19
  37, // P1.05 = GPIO 20
  13, // P0.13 = GPIO 21
  15  // P0.15 = GPIO 22
};

void initVariant() {}
