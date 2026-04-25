#ifndef _MESHTINY_H_
#define _MESHTINY_H_

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
#define PIN_LED1                        (35) // Green LED
#define PIN_LED2                        (36) // Blue LED

#define LED_RED                         (-1)
#define LED_GREEN                       PIN_LED1
#define LED_BLUE                        (-1)  // Disable annoying flashing caused by Bluefruit

#define P_LORA_TX_LED                   PIN_LED2 // Blue LED
// #define PIN_STATUS_LED                  LED_GREEN  // disable status led.
#define LED_BUILTIN                     LED_GREEN
#define PIN_LED                         LED_BUILTIN
#define LED_PIN                         LED_BUILTIN
#define LED_STATE_ON                    HIGH

// Buttons
#define PIN_BUTTON1                     (9)   // side button
#define PIN_BUTTON2                     (4)   // encoder left
#define PIN_BUTTON3                     (26)  // encoder right
#define PIN_BUTTON4                     (28)  // encoder press
#define PIN_SIDE_BUTTON                 PIN_BUTTON1
#define ENCODER_LEFT                    PIN_BUTTON2
#define ENCODER_RIGHT                   PIN_BUTTON3
#define ENCODER_PRESS                   PIN_BUTTON4
#define PIN_USER_BTN                    PIN_SIDE_BUTTON

// VBAT sensing
#define PIN_VBAT_READ                   (5)
#define BATTERY_SENSE_RESOLUTION_BITS   12
#define BATTERY_SENSE_RESOLUTION        4096.0
#define AREF_VOLTAGE                    3.0
#define VBAT_AR_INTERNAL                AR_INTERNAL_3_0
#define ADC_MULTIPLIER                  1.73
#define ADC_RESOLUTION                  14

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

#define PIN_SPI1_MISO                   (29)
#define PIN_SPI1_MOSI                   (30)
#define PIN_SPI1_SCK                    (3)

// LoRa SX1262 module pins
#define P_LORA_SCLK                     PIN_SPI_SCK
#define P_LORA_MISO                     PIN_SPI_MISO
#define P_LORA_MOSI                     PIN_SPI_MOSI
#define P_LORA_DIO_1                    (47)
#define P_LORA_RESET                    (38)
#define P_LORA_BUSY                     (46)
#define P_LORA_NSS                      (42)
#define SX126X_POWER_EN                 (37)

#define SX126X_RXEN                     RADIOLIB_NC
#define SX126X_TXEN                     RADIOLIB_NC

#define SX126X_DIO2_AS_RF_SWITCH        true
#define SX126X_DIO3_TCXO_VOLTAGE        (1.8f)

// Wire Interfaces
#define WIRE_INTERFACES_COUNT           1
#define PIN_WIRE_SDA                    (13)
#define PIN_WIRE_SCL                    (14)
#define PIN_BOARD_SDA                   (13)
#define PIN_BOARD_SCL                   (14)

// Power control
#define PIN_3V3_EN                      (34)    // nothing connected on meshtiny board

#endif // _MESHTINY_H_
