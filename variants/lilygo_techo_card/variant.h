/*
 * variant.h -- LilyGo T-Echo Card pin definitions
 *
 * nRF52840 + SX1262 (HPB16B3) + SSD1315 OLED (72x40) + L76K GPS
 * + MAX98357 Speaker + MP34DT05 PDM Mic + ICM20948 IMU + BQ25896 + Solar
 *
 * Cross-referenced against:
 *   - LilyGo official: T-Echo-Card/libraries/private_library/t_echo_card_config.h
 *   - Meshtastic PR #10267 (caveman99)
 */

#pragma once

#include "WVariant.h"

////////////////////////////////////////////////////////////////////////////////
// Low frequency clock source

#define USE_LFXO      // 32.768 kHz crystal
#define VARIANT_MCK   (64000000ul)

////////////////////////////////////////////////////////////////////////////////
// Power / Battery

#define PIN_VBAT_READ               2     // (0, 2) = AIN0
#define BATTERY_ADC_AIN             0     // nRF SAADC AIN channel number

// Gated voltage divider: drive HIGH before ADC read, LOW after
#define PIN_BAT_CTL                 31    // (0, 31)
#define ADC_MULTIPLIER              (2.0F)

#define MV_LSB                      (3000.0F / 4096.0F)

#define ADC_RESOLUTION              (14)
#define BATTERY_SENSE_RES           (12)
#define AREF_VOLTAGE                (3.0)

////////////////////////////////////////////////////////////////////////////////
// Pin counts

#define PINS_COUNT                  (48)
#define NUM_DIGITAL_PINS            (48)
#define NUM_ANALOG_INPUTS           (1)
#define NUM_ANALOG_OUTPUTS          (0)

////////////////////////////////////////////////////////////////////////////////
// UART -- GPS (L76K)

#define PIN_SERIAL1_RX              19    // (0, 19) -- GPS TX -> nRF RX
#define PIN_SERIAL1_TX              21    // (0, 21) -- nRF TX -> GPS RX

////////////////////////////////////////////////////////////////////////////////
// I2C (shared: OLED, IMU ICM20948)

#define WIRE_INTERFACES_COUNT       (1)
#define PIN_WIRE_SDA                36    // (1, 4)
#define PIN_WIRE_SCL                34    // (1, 2)

////////////////////////////////////////////////////////////////////////////////
// LEDs -- WS2812 addressable (no plain GPIO LED)
// The BSP drives LED_BUILTIN via digitalWrite for BLE status -- if pointed at
// the WS2812 data pin (39), it holds the line HIGH and all LEDs glow green.
// Point at an unused GPIO (46 = P1.14) so the BSP toggles harmlessly.

#define LED_BUILTIN                 46    // Unused GPIO -- keeps BSP happy
#define PIN_LED                     LED_BUILTIN
#define LED_RED                     LED_BUILTIN
#define LED_BLUE                    (-1)  // Prevents Bluefruit flashing during advertising
#define PIN_STATUS_LED              LED_BUILTIN
#define LED_STATE_ON                1

// WS2812 RGB LEDs -- 3 LEDs daisy-chained on a single data line (pin 39)
// Hardware verified: all three light when pin 39 is driven HIGH.
// Meshtastic PR #10267 mapped them as separate GPIOs (39, 44, 28) but
// testing confirms they're chained.
#define HAS_RGB_LED                 1
#define PIN_RGB_LED_1               39    // (1, 7) -- chain data in
#define PIN_NEOPIXEL                PIN_RGB_LED_1
#define NUM_NEOPIXELS               3

////////////////////////////////////////////////////////////////////////////////
// Buttons

#define PIN_BUTTON1                 42    // (1, 10) -- orange front button
#define BUTTON_PIN                  PIN_BUTTON1
#define PIN_USER_BTN                BUTTON_PIN
// Boot button: P0.24 (hardware only, used for DFU)

////////////////////////////////////////////////////////////////////////////////
// SPI -- LoRa

#define SPI_INTERFACES_COUNT        (1)

#define PIN_SPI_MISO                17    // (0, 17)
#define PIN_SPI_MOSI                15    // (0, 15)
#define PIN_SPI_SCK                 13    // (0, 13)

////////////////////////////////////////////////////////////////////////////////
// SX1262 LoRa Radio (HPB16B3 / S62F module)

#define USE_SX1262
#define SX126X_CS                   11    // (0, 11)
#define SX126X_DIO1                 40    // (1, 8)
#define SX126X_BUSY                 14    // (0, 14)
#define SX126X_RESET                7     // (0, 7)
#define SX126X_DIO2_AS_RF_SWITCH    true
#define SX126X_DIO3_TCXO_VOLTAGE    1.8

#define P_LORA_NSS                  SX126X_CS
#define P_LORA_DIO_1                SX126X_DIO1
#define P_LORA_RESET                SX126X_RESET
#define P_LORA_BUSY                 SX126X_BUSY
#define P_LORA_SCLK                 PIN_SPI_SCK
#define P_LORA_MISO                 PIN_SPI_MISO
#define P_LORA_MOSI                 PIN_SPI_MOSI

// RF switch control lines (may be needed in addition to DIO2)
#define LORA_RF_VC1                 27    // (0, 27)
#define LORA_RF_VC2                 33    // (1, 1)

////////////////////////////////////////////////////////////////////////////////
// OLED Display -- SSD1315 (SSD1306-compatible), 72x40, I2C
//
// Physical panel is 72x40 within 128x64 GDDRAM.
// Visible window: columns 28–99, pages 3–7 (rows 24–63).
// SETDISPLAYOFFSET = 24 maps page 0 writes to the visible area.

#define HAS_OLED                    1
#define OLED_I2C_ADDR               0x3C
#define OLED_WIDTH                  72
#define OLED_HEIGHT                 40
#define OLED_DISPLAY_OFFSET         24

// RT9080 enable -- controls 3V3 rail (OLED, GPS, LoRa, sensors)
#define PIN_OLED_EN                 30    // (0, 30)
#define PIN_OLED_RESET              (-1)

////////////////////////////////////////////////////////////////////////////////
// GPS -- L76K Multi-GNSS

#define HAS_GPS                     1
#define GPS_BAUDRATE                9600
#define PIN_GPS_TX                  21    // nRF TX -> GPS RX (vendor GPS_UART_RX / P0.21)
#define PIN_GPS_RX                  19    // nRF RX <- GPS TX (vendor GPS_UART_TX / P0.19)
#define PIN_GPS_EN                  47    // (1, 15)
#define PIN_GPS_WAKEUP              25    // (0, 25)
#define PIN_GPS_1PPS                23    // (0, 23)
#define PIN_GPS_RF_EN               29    // (0, 29)

////////////////////////////////////////////////////////////////////////////////
// Speaker -- MAX98357 I2S Class-D Mono Amp

#define HAS_SPEAKER                 1
#define PIN_SPK_EN                  43    // (1, 11)
#define PIN_SPK_EN2                 3     // (0, 3)
#define PIN_SPK_BCLK                16    // (0, 16)
#define PIN_SPK_DATA                20    // (0, 20)
#define PIN_SPK_LRCK                22    // (0, 22)

////////////////////////////////////////////////////////////////////////////////
// Microphone -- MP34DT05 Digital MEMS PDM

#define HAS_MICROPHONE              1
#define PIN_MIC_CLK                 35    // (1, 3)
#define PIN_MIC_DATA                37    // (1, 5)

////////////////////////////////////////////////////////////////////////////////
// Buzzer

#ifndef HAS_BUZZER
#define HAS_BUZZER                  1
#endif
#ifndef PIN_BUZZER
#define PIN_BUZZER                  38    // (1, 6)
#endif

////////////////////////////////////////////////////////////////////////////////
// IMU -- ICM20948

#define HAS_IMU                     1
#define IMU_I2C_ADDR                0x68

////////////////////////////////////////////////////////////////////////////////
// NFC -- nRF52840 NFC-A (dedicated P0.09/P0.10)

#define HAS_NFC                     1

////////////////////////////////////////////////////////////////////////////////
// External Flash -- ZD25WQ32CEIGR 4MB QSPI

#define HAS_EXT_FLASH               1
#define PIN_QSPI_SCK                4     // (0, 4)
#define PIN_QSPI_CS                 12    // (0, 12)
#define PIN_QSPI_IO0                6     // (0, 6)
#define PIN_QSPI_IO1                8     // (0, 8)
#define PIN_QSPI_IO2                41    // (1, 9)
#define PIN_QSPI_IO3                26    // (0, 26)

////////////////////////////////////////////////////////////////////////////////
// No dedicated RTC chip -- time from GPS or BLE companion sync

#define HAS_RTC                     0