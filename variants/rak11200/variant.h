#pragma once

// RAK11200 WisBlock ESP32 Pin Definitions

// I2C Interface (Main board I2C)
#define I2C_SDA 21
#define I2C_SCL 22

// GPS Interface
#define GPS_TX_PIN 1
#define GPS_RX_PIN 3
#define PIN_GPS_EN -1
#define GPS_POWER_TOGGLE
#define GPS_BAUD_RATE 9600

// User Interface
#define BUTTON_PIN 34       // WB_SW1 - User button
#define BATTERY_PIN 36      // WB_A0 - Battery voltage measurement pin
#define ADC_CHANNEL ADC1_GPIO36_CHANNEL
#define ADC_MULTIPLIER 1.85 // Adjust based on voltage divider
#define LED_PIN 12          // WB_LED1 - Status LED
#define LED_PIN_2 2         // WB_LED2 - Secondary LED

// LoRa radio module pins for RAK11200 + RAK13300 SX1262
#define P_LORA_DIO_1   22   // WB_IO6 - DIO1 interrupt pin
#define P_LORA_NSS     32   // WB_CS - SPI Chip Select
#define P_LORA_RESET   23   // WB_IO4 - Reset pin
#define P_LORA_BUSY    RADIOLIB_NC  // Not connected - using DIO2 for RF switching
#define P_LORA_SCLK    33   // SPI Clock
#define P_LORA_MISO    35   // SPI MISO
#define P_LORA_MOSI    25   // SPI MOSI

// LoRa radio configuration
#define USE_SX1262
#define SX126X_MAX_POWER 22
#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define SX126X_CURRENT_LIMIT 140

// Compatibility defines for variant file configuration structure
#define LORA_CS P_LORA_NSS
#define LORA_SCK P_LORA_SCLK
#define LORA_MOSI P_LORA_MOSI
#define LORA_MISO P_LORA_MISO
#define LORA_DIO1 P_LORA_DIO_1

// WisBlock GPIO mapping for RAK11200
#define WB_IO1 14    // GPIO14
#define WB_IO2 27    // GPIO27
#define WB_IO3 26    // GPIO26
#define WB_IO4 23    // GPIO23
#define WB_IO5 13    // GPIO13
#define WB_IO6 22    // GPIO22
#define WB_SW1 34    // GPIO34 - Switch/Button
#define WB_A0  36    // GPIO36 - Analog input
#define WB_A1  39    // GPIO39 - Analog input
#define WB_CS  32    // GPIO32 - SPI Chip Select
#define WB_LED1 12   // GPIO12 - LED1
#define WB_LED2 2    // GPIO2  - LED2

// Board identification
#define RAK_11200
#define RAK_BOARD
