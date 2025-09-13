#ifndef _SEEED_WIO_TRACKER_L1_H_
#define _SEEED_WIO_TRACKER_L1_H_

/** Master clock frequency */
#define VARIANT_MCK             (64000000ul)

#define USE_LFXO                // Board uses 32khz crystal for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#define PINS_COUNT              (33)
#define NUM_DIGITAL_PINS        (33)
#define NUM_ANALOG_INPUTS       (8)
#define NUM_ANALOG_OUTPUTS      (0)

// LEDs
#define PIN_LED                 (11)
#define LED_BLUE                (-1) // Disable annoying flashing caused by Bluefruit
#define LED_BUILTIN             PIN_LED
#define P_LORA_TX_LED           PIN_LED
#define LED_STATE_ON            1

// Buttons
#define PIN_BUTTON1             (13) // Menu / User Button
#define PIN_BUTTON2             (25) // Joystick Up
#define PIN_BUTTON3             (26) // Joystick Down
#define PIN_BUTTON4             (27) // Joystick Left
#define PIN_BUTTON5             (28) // Joystick Right
#define PIN_BUTTON6             (29) // Joystick Press
#define PIN_USER_BTN            PIN_BUTTON1
#define JOYSTICK_UP             PIN_BUTTON2
#define JOYSTICK_DOWN           PIN_BUTTON3
#define JOYSTICK_LEFT           PIN_BUTTON4
#define JOYSTICK_RIGHT          PIN_BUTTON5
#define JOYSTICK_PRESS          PIN_BUTTON6

// Buzzer
// #define PIN_BUZZER           (12) // Buzzer pin (defined per firmware type)

#define VBAT_ENABLE             (30)

// Analog pins
#define PIN_VBAT_READ           (16)
#define AREF_VOLTAGE            (3.6F)
#define ADC_MULTIPLIER          (2.0F)
#define ADC_RESOLUTION          (12)

// Serial interfaces
#define PIN_SERIAL1_RX          (7)
#define PIN_SERIAL1_TX          (6)

// SPI Interfaces
#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (9)
#define PIN_SPI_MOSI            (10)
#define PIN_SPI_SCK             (8)

// Lora Pins
#define  P_LORA_SCLK            PIN_SPI_SCK
#define  P_LORA_MISO            PIN_SPI_MISO
#define  P_LORA_MOSI            PIN_SPI_MOSI
#define  P_LORA_DIO_1           (1)
#define  P_LORA_RESET           (2)
#define  P_LORA_BUSY            (3)
#define  P_LORA_NSS             (4)
#define  SX126X_RXEN            (5)
#define  SX126X_TXEN            RADIOLIB_NC
#define  SX126X_DIO2_AS_RF_SWITCH true
#define  SX126X_DIO3_TCXO_VOLTAGE (1.8f)

// Wire Interfaces
#define WIRE_INTERFACES_COUNT   (2)

#define PIN_WIRE_SDA            (14)
#define PIN_WIRE_SCL            (15)
#define PIN_WIRE1_SDA           (17)
#define PIN_WIRE1_SCL           (18)
#define I2C_NO_RESCAN
#define DISPLAY_ADDRESS         0x3D  // SH1106 OLED I2C address

// GPS L76KB
#define GPS_BAUDRATE            9600
#define PIN_GPS_TX              PIN_SERIAL1_RX
#define PIN_GPS_RX              PIN_SERIAL1_TX
#define PIN_GPS_STANDBY         (0)
#define PIN_GPS_EN              (18)

// QSPI Pins
#define PIN_QSPI_SCK            (19)
#define PIN_QSPI_CS             (20)
#define PIN_QSPI_IO0            (21)
#define PIN_QSPI_IO1            (22)
#define PIN_QSPI_IO2            (23)
#define PIN_QSPI_IO3            (24)

#define EXTERNAL_FLASH_DEVICES P25Q16H
#define EXTERNAL_FLASH_USE_QSPI

#endif