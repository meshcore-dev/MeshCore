#pragma once

// RAK11200 WisBlock ESP32 Pin Definitions
// Reference: https://docs.rakwireless.com/Product-Categories/WisBlock/RAK11200/Datasheet/
// Reference: https://docs.rakwireless.com/Product-Categories/WisBlock/RAK13300/Datasheet/

// I2C Interface (I2C1 on WisBlock connector pins 19/20)
#define I2C_SDA 4   // GPIO4
#define I2C_SCL 5   // GPIO5

// GPS Interface (UART1)
#define GPS_TX_PIN 21
#define GPS_RX_PIN 19
#define PIN_GPS_EN -1
#define GPS_BAUD_RATE 9600

// User Interface
#define BUTTON_PIN 34       // WB_SW1 - User button
#define BATTERY_PIN 36      // WB_A0 - Battery voltage measurement pin
#define ADC_CHANNEL ADC1_GPIO36_CHANNEL
#define LED_PIN 12          // WB_LED1 - Status LED
#define LED_PIN_2 2         // WB_LED2 - Secondary LED

// Board identification
#define RAK_11200
#define RAK_BOARD
