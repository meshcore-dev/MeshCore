#ifndef _KEEPTEEN_LT1_H_
#define _KEEPTEEN_LT1_H_

/** Master clock frequency */
#define VARIANT_MCK             (64000000ul)
#define USE_LFRC

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (1)
#define NUM_ANALOG_OUTPUTS      (0)

// LEDs
#define PIN_LED                 (15) // Blue LED
#define PIN_LED2                (13) // Maybe red power LED?
#define LED_BLUE                (-1) // Disable annoying flashing caused by Bluefruit
#define LED_BUILTIN             PIN_LED
#define P_LORA_TX_LED           PIN_LED
#define LED_STATE_ON            1

// Buttons
#define PIN_BUTTON1             (32) // Menu / User Button
#define PIN_USER_BTN            PIN_BUTTON1

// Analog pins
#define PIN_VBAT_READ           (31)
#define AREF_VOLTAGE            (3.6F)
#define ADC_MULTIPLIER          (1.535F)
#define ADC_RESOLUTION          (12)

// Serial interfaces
#define PIN_SERIAL1_RX          (22)
#define PIN_SERIAL1_TX          (20)

// SPI Interfaces
#define SPI_INTERFACES_COUNT    (1)

#define PIN_SPI_MISO            (2)
#define PIN_SPI_MOSI            (38)
#define PIN_SPI_SCK             (43)

// Lora Pins
#define  P_LORA_BUSY            (29)
#define  P_LORA_MISO            PIN_SPI_MISO
#define  P_LORA_MOSI            PIN_SPI_MOSI
#define  P_LORA_NSS             (45)
#define  P_LORA_SCLK            PIN_SPI_SCK
#define  P_LORA_DIO_1           (10)
#define  P_LORA_RESET           (9)
#define  SX126X_RXEN            RADIOLIB_NC
#define  SX126X_TXEN            RADIOLIB_NC
#define  SX126X_DIO2_AS_RF_SWITCH true
#define  SX126X_DIO3_TCXO_VOLTAGE (1.8f)

// Wire Interfaces
#define WIRE_INTERFACES_COUNT   (1)

#define PIN_WIRE_SDA            (34)
#define PIN_WIRE_SCL            (36)
#define I2C_NO_RESCAN

// GPS L76KB
#define GPS_BAUDRATE            9600
#define PIN_GPS_TX              PIN_SERIAL1_RX
#define PIN_GPS_RX              PIN_SERIAL1_TX
#define PIN_GPS_EN              (24)

#endif // _KEEPTEEN_LT1_H_