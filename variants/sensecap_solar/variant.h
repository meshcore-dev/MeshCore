#ifndef _SEEED_SENSECAP_SOLAR_H_
#define _SEEED_SENSECAP_SOLAR_H_

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
#define PIN_LED                 (12)
#define LED_PWR                 (PINS_COUNT)

#define LED_BUILTIN             (PIN_LED)

#define LED_RED                 (PINS_COUNT)
#define LED_GREEN               (12)
#define LED_BLUE                (11)

#define LED_STATE_ON            (1)     // State when LED is litted

// Buttons
#define PIN_BUTTON1             (13)
#define PIN_BUTTON2             (20)

#define VBAT_ENABLE             (19)    // Output LOW to enable reading of the BAT voltage.

// Analog pins
#define BATTERY_PIN             (16)    // Read the BAT voltage.
#define AREF_VOLTAGE            (3.0F)
#define ADC_MULTIPLIER          (3.0F) // 1M, 512k divider bridge
#define ADC_RESOLUTION          (12)

// Serial interfaces
#define PIN_SERIAL1_RX          (7)
#define PIN_SERIAL1_TX          (6)

// SPI Interfaces
#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (9)
#define PIN_SPI_MOSI            (10)
#define PIN_SPI_SCK             (8)

// Lora SPI is on SPI0
#define  P_LORA_SCLK            PIN_SPI_SCK
#define  P_LORA_MISO            PIN_SPI_MISO
#define  P_LORA_MOSI            PIN_SPI_MOSI

// Wire Interfaces
#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (14)
#define PIN_WIRE_SCL            (15)

// GPS L76KB
#define GPS_BAUDRATE            9600
#define GPS_THREAD_INTERVAL     50
#define PIN_GPS_TX              PIN_SERIAL1_RX
#define PIN_GPS_RX              PIN_SERIAL1_TX
#define PIN_GPS_STANDBY         (0)
#define GPS_EN                  (18)

// QSPI Pins
#define PIN_QSPI_SCK            (21)
#define PIN_QSPI_CS             (22)
#define PIN_QSPI_IO0            (23)
#define PIN_QSPI_IO1            (24)
#define PIN_QSPI_IO2            (25)
#define PIN_QSPI_IO3            (26)

#define EXTERNAL_FLASH_DEVICES P25Q16H
#define EXTERNAL_FLASH_USE_QSPI

#endif