#pragma once

// Elecrow ThinkNode G3 (ILM13103D)
// ESP32-S3-WROOM-1-N8R8, LR1262 module (SX1262 + TCXO + RF switch)
//
// Pin values recovered from stock Elecrow firmware constants, not from a
// published schematic. Confirm against your own flash dump before flashing.

#define P_LORA_NSS    39
#define P_LORA_SCLK   42
#define P_LORA_MOSI   40
#define P_LORA_MISO   41
#define P_LORA_RESET  21
#define P_LORA_BUSY   47
#define P_LORA_DIO_1  15

// Stock firmware drives this high once at init (antenna_sw / PE_EN).
#define P_LORA_EN     45

#define PIN_USER_BTN    4   // Reload, silk K2
#define PIN_STATUS_LED  5   // LINK, green
#define P_LORA_TX_LED   6   // LoRa LED

#define PIN_BOARD_SDA  17
#define PIN_BOARD_SCL  18

// USB D-/D+ are GPIO 19/20. Do not reuse.
// BOOT is WROOM IO0. Do not use as a radio pin.
