#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
  8, //P0.08 - D0 
  6, //P0.06 - D1 
  17,//P0.17 - D2
  20,//P0.20 - D3 
  22,//P0.22 - D4 
  24,//P0.24 - D5
  32,//P1.00 - D6
  11,//P0.11 - D7
  36,//P1.04 - D8
  38,//P1.06 - D9
  9, //P0.09 - D10
  10,//P0.10 - D11
  43,//P1.11 - D12
  45,//P1.13 - D13
  47,//P1.15 - D14
  2, //P0.02 - D15
  29,//P0.29 - D16
  31,//P0.31 - D17
  33,//P1.01 - 101 - D18 - joystic right
  34,//P1.02 - 102 - D19 - joystic center
  37,//P1.05 - 105 - D20
  39,//P1.07 - 107 - D21 - joystic left
  13,//P0.13 - D22 - POWER_PIN
  15 //P0.15 - D23 - BLED
};

void initVariant()
{
}

