#pragma once

#include <variant_generic.h>

#define LORAWAN_RFSWITCH_PINS           PA7, PA6 // RX_EN, TX_EN
#define LORAWAN_RFSWITCH_PIN_COUNT      2
#define LORAWAN_RFSWITCH_OFF_VALUES     LOW, LOW
#define LORAWAN_RFSWITCH_RX_VALUES      HIGH, LOW
#define LORAWAN_RFSWITCH_RFO_LP_VALUES  LOW, HIGH
#define LORAWAN_RFSWITCH_RFO_HP_VALUES  LOW, HIGH

#define STM32WL_TCXO_VOLTAGE            1.6

#define P_LORA_TX_LED                   PB4 // LED1
#define P_LORA_RX_LED                   PB3 // LED2 -- not used
#define PIN_BUTTON1                     PA0 // Just information
#define PIN_BUTTON2                     PA1 // Just information
// PA2 UART2_TX
// PA3 UART2_RX
// PB7 UART1_RX => USB port
// PB6 UART1_TX => USB port

#undef RNG
