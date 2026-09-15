#include "variant.h"
#include "Arduino.h"

void initVariant()
{
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, HIGH);
}