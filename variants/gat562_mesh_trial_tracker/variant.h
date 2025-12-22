#ifndef _GAT562_MESH_TRIAL_TRACKER_H_
#define _GAT562_MESH_TRIAL_TRACKER_H_

/** Master clock frequency */
#define VARIANT_MCK                     (64000000ul)

#define USE_LFXO                        // Board uses 32khz crystal for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#define PINS_COUNT                      (48)
#define NUM_DIGITAL_PINS                (48)
#define NUM_ANALOG_INPUTS               (6)
#define NUM_ANALOG_OUTPUTS              (0)

// LEDs
#define PIN_LED1                        (35)
#define PIN_LED2                        (36)

#define LED_RED                         (-1)
#define LED_GREEN                       PIN_LED1
#define LED_BLUE                        PIN_LED2

#define P_LORA_TX_LED                   LED_BLUE
#define PIN_STATUS_LED                  LED_GREEN
#define LED_BUILTIN                     LED_GREEN
#define PIN_LED                         LED_BUILTIN
#define LED_PIN                         LED_BUILTIN
#define LED_STATE_ON                    HIGH

// Buttons
#define PIN_BUTTON1                     (9)     // Pin for button on E-ink button module or IO expansion
// #define PIN_BUTTON2                     (12)
// #define PIN_BUTTON3                     (24)
// #define PIN_BUTTON4                     (25)
#define PIN_USER_BTN                    PIN_BUTTON1
// #define BUTTON_NEED_PULLUP

// Analog pins
#define PIN_A0                          (5)
// #define PIN_A1                          (31)   // Duplicate pin assignment
// #define PIN_A2                          (28)
// #define PIN_A3                          (29)
// #define PIN_A4                          (30)
// #define PIN_A5                          (31)   // Duplicate pin assignment
// #define PIN_A6                          (0xff)
// #define PIN_A7                          (0xff)

// Battery
#define BATTERY_PIN                     PIN_A0
#define PIN_VBAT_READ                   BATTERY_PIN
#define BATTERY_SENSE_RESOLUTION_BITS   12
#define BATTERY_SENSE_RESOLUTION        4096.0
#define AREF_VOLTAGE                    3.0
#define VBAT_AR_INTERNAL                AR_INTERNAL_3_0
#define ADC_MULTIPLIER                  1.73
#define ADC_RESOLUTION                  14

// Other pins
// #define PIN_AREF                        (2)
// #define PIN_NFC1                        (9)    // Conflicts with PIN_BUTTON1
// #define PIN_NFC2                        (10)

// Serial interfaces
#define PIN_SERIAL1_RX                  (15)
#define PIN_SERIAL1_TX                  (16)
#define PIN_SERIAL2_RX                  (8)     // Connected to Jlink CDC
#define PIN_SERIAL2_TX                  (6)

// SPI Interfaces
#define SPI_INTERFACES_COUNT            2

#define PIN_SPI_MISO                    (45)
#define PIN_SPI_MOSI                    (44)
#define PIN_SPI_SCK                     (43)

#define PIN_SPI1_MISO                   (29)    // (0 + 29)
#define PIN_SPI1_MOSI                   (30)    // (0 + 30)
#define PIN_SPI1_SCK                    (3)     // (0 + 3)

// LoRa SX1262 module pins
#define P_LORA_SCLK                     PIN_SPI_SCK
#define P_LORA_MISO                     PIN_SPI_MISO
#define P_LORA_MOSI                     PIN_SPI_MOSI
#define P_LORA_DIO_1                    (47)
#define P_LORA_RESET                    (38)
#define P_LORA_BUSY                     (46)
#define P_LORA_NSS                      (42)
#define SX126X_RXEN                     (37)
#define SX126X_TXEN                     RADIOLIB_NC
#define SX126X_DIO2_AS_RF_SWITCH        true
#define SX126X_DIO3_TCXO_VOLTAGE        (1.8f)

// Wire Interfaces
#define WIRE_INTERFACES_COUNT           1
#define PIN_WIRE_SDA                    (13)
#define PIN_WIRE_SCL                    (14)
#define PIN_BOARD_SDA                   (13)
#define PIN_BOARD_SCL                   (14)

// Second I2C interface (not available on this board)
// #define PIN_WIRE1_SDA                   (xx)
// #define PIN_WIRE1_SCL                   (xx)

// Display
// #define HAS_SCREEN                      1
// #define USE_SSD1306
// #define PIN_OLED_RESET                  (-1)

// Power control
#define PIN_3V3_EN                      (34)    // enables 3.3V periphery like GPS or IO Module

// GPS L76K
#define GPS_BAUD_RATE                   9600
#define PIN_GPS_TX                      PIN_SERIAL1_TX  // Uses Serial1 TX pin
#define PIN_GPS_RX                      PIN_SERIAL1_RX  // Uses Serial1 RX pin
#define PIN_GPS_EN                      PIN_3V3_EN
#define PIN_GPS_PPS                     (17)    // Pulse per second input from the GPS

// QSPI Pins
// #define PIN_QSPI_SCK                    (3)
// #define PIN_QSPI_CS                     (26)
// #define PIN_QSPI_IO0                    (30)
// #define PIN_QSPI_IO1                    (29)
// #define PIN_QSPI_IO2                    (28)
// #define PIN_QSPI_IO3                    (2)

// On-board QSPI Flash
// #define EXTERNAL_FLASH_DEVICES          W25Q16JV_IQ
// #define EXTERNAL_FLASH_USE_QSPI

// Testing USB detection
// #define NRF_APM

#endif
