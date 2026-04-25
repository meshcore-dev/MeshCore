#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
  0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
  14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47
};

void initVariant()
{
    // set LED pin as output and set it low for off
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    // set INPUT_PULLUP for user button
    pinMode(PIN_USER_BTN, INPUT_PULLUP);
    
}

