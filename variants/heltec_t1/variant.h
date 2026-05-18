/*
 * Heltec Mesh Node T1 nRF52840 variant.
 */

#pragma once

#include "WVariant.h"

#define USE_LFXO
#define VARIANT_MCK (64000000ul)

////////////////////////////////////////////////////////////////////////////////
// Number of pins

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (1)
#define NUM_ANALOG_OUTPUTS      (0)

////////////////////////////////////////////////////////////////////////////////
// Display

#define ST7735_CS               (0 + 12)
#define ST7735_RS               (0 + 22)
#define ST7735_SDA              (0 + 24)
#define ST7735_SCK              (32 + 0)
#define ST7735_RESET            (0 + 20)
#define ST7735_MISO             (-1)
#define ST7735_BUSY             (-1)
#define ST7735_BL               (0 + 15)
#define VTFT_CTRL               (0 + 13)

#define PIN_TFT_CS              ST7735_CS
#define PIN_TFT_DC              ST7735_RS
#define PIN_TFT_SDA             ST7735_SDA
#define PIN_TFT_SCL             ST7735_SCK
#define PIN_TFT_RST             ST7735_RESET
#define PIN_TFT_LEDA_CTL        ST7735_BL
#define PIN_TFT_LEDA_CTL_ACTIVE LOW
#define PIN_TFT_VDD_CTL         VTFT_CTRL
#define PIN_TFT_VDD_CTL_ACTIVE  LOW
#define DISPLAY_ROTATION        3

////////////////////////////////////////////////////////////////////////////////
// Builtin LEDs

#define PIN_LED1                (0 + 16)
#define LED_BUILTIN             PIN_LED1
#define PIN_LED                 LED_BUILTIN
#define LED_RED                 (-1)
#define LED_BLUE                (-1)
#define LED_GREEN               (-1)
#define LED_PIN                 LED_BUILTIN
#define LED_STATE_ON            LOW

////////////////////////////////////////////////////////////////////////////////
// Builtin buttons

#define PIN_BUTTON1             (32 + 10)
#define PIN_BUTTON2             (0 + 14)
#define BUTTON_PIN              PIN_BUTTON1
#define BUTTON_PIN2             PIN_BUTTON2
#define PIN_USER_BTN            BUTTON_PIN

////////////////////////////////////////////////////////////////////////////////
// UART

// No longer populated on PCB.
#define PIN_SERIAL2_RX          (-1)
#define PIN_SERIAL2_TX          (-1)

////////////////////////////////////////////////////////////////////////////////
// I2C

#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (32 + 3)
#define PIN_WIRE_SCL            (0 + 10)

#define PIN_SENSOR_EN           (32 + 6)
#define PIN_SENSOR_EN_ACTIVE    LOW

////////////////////////////////////////////////////////////////////////////////
// LoRa

#define USE_SX1262
#define SX126X_CS               (32 + 11)
#define LORA_CS                 SX126X_CS
#define SX126X_DIO1             (0 + 31)
#define SX126X_BUSY             (0 + 29)
#define SX126X_RESET            (0 + 2)
#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define SX126X_CURRENT_LIMIT    140
#define SX126X_RX_BOOSTED_GAIN  1

#define P_LORA_DIO_1            SX126X_DIO1
#define P_LORA_NSS              LORA_CS
#define P_LORA_RESET            SX126X_RESET
#define P_LORA_BUSY             SX126X_BUSY
#define P_LORA_TX_LED           PIN_LED1

////////////////////////////////////////////////////////////////////////////////
// SPI

#define SPI_INTERFACES_COUNT    (2)

#define PIN_SPI_MISO            (0 + 3)
#define PIN_SPI_MOSI            (32 + 14)
#define PIN_SPI_SCK             (32 + 13)
#define PIN_SPI_NSS             LORA_CS

#define PIN_SPI1_MISO           ST7735_MISO
#define PIN_SPI1_MOSI           ST7735_SDA
#define PIN_SPI1_SCK            ST7735_SCK

#define P_LORA_SCLK             PIN_SPI_SCK
#define P_LORA_MISO             PIN_SPI_MISO
#define P_LORA_MOSI             PIN_SPI_MOSI

////////////////////////////////////////////////////////////////////////////////
// GPS

#define GPS_UC6580
#define GPS_BAUD_RATE           115200
#define PIN_GPS_RESET           (0 + 26)
#define GPS_RESET               PIN_GPS_RESET
#define GPS_RESET_MODE          LOW
#define PIN_GPS_RESET_ACTIVE    LOW
#define PIN_GPS_EN              (0 + 4)
#define GPS_EN                  PIN_GPS_EN
#define GPS_EN_ACTIVE           LOW
#define PIN_GPS_EN_ACTIVE       LOW
#define PIN_GPS_PPS             (32 + 9)
#define GPS_TX_PIN              (0 + 8)
#define GPS_RX_PIN              (0 + 7)
#define PIN_GPS_TX              GPS_TX_PIN
#define PIN_GPS_RX              GPS_RX_PIN

#define PIN_SERIAL1_RX          GPS_RX_PIN
#define PIN_SERIAL1_TX          GPS_TX_PIN

////////////////////////////////////////////////////////////////////////////////
// Buzzer

#define PIN_BUZZER              (0 + 9)
#define PIN_BUZZER_VOLTAGE_MULTIPLIER_1 (32 + 2)
#define PIN_BUZZER_VOLTAGE_MULTIPLIER_2 (32 + 5)

////////////////////////////////////////////////////////////////////////////////
// Battery

#define ADC_CTRL                (0 + 11)
#define PIN_BAT_CTL             ADC_CTRL
#define ADC_CTRL_ENABLED        HIGH
#define BATTERY_PIN             (0 + 5)
#define PIN_VBAT_READ           BATTERY_PIN
#define ADC_RESOLUTION          (14)

#define AREF_VOLTAGE            (3.0)
#define VBAT_AR_INTERNAL        AR_INTERNAL_3_0
#define ADC_MULTIPLIER          (4.916F)
#define MV_LSB                  (3000.0F / 4096.0F)

#define PWRMGT_VOLTAGE_BOOTLOCK 3100
#define PWRMGT_LPCOMP_AIN       3
#define PWRMGT_LPCOMP_REFSEL    1

#define HAS_RTC                 0
