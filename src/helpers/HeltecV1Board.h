#pragma once

#include <Arduino.h>
#include "ESP32Board.h"
#include <driver/rtc_io.h>

// LoRa radio module pins for Heltec V1
#define LORA_CS      18   // NSS
#define LORA_RST     14   // RESET
#define LORA_DIO0    26   // DIO0
#define LORA_DIO1    35   // DIO1
#define LORA_DIO2    34   // DIO2
#define LORA_SCK     5    // SCK
#define LORA_MISO    19   // MISO
#define LORA_MOSI    27   // MOSI

#define PIN_OLED_SDA 4
#define PIN_OLED_SCL 15
#define PIN_OLED_RESET 16
#define PIN_USER_BTN 0
#define PIN_LED_BUILTIN 25

// Default LoRa parameters for RadioLib
#define LORA_FREQ 868.0
#define LORA_BW   125.0
#define LORA_SF   7

class HeltecV1Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();
    // Add any Heltec V1 specific startup logic here
  }
};
