/*
 * variant.h
 * Copyright (C) 2023 Seeed K.K.
 * MIT License
 */

 #pragma once

 #include "WVariant.h"

 ////////////////////////////////////////////////////////////////////////////////
 // Low frequency clock source

#define VARIANT_MCK       (64000000ul)

#if defined(PROMICRO)

    //#define USE_LFXO      // 32.768 kHz crystal oscillator
    #define USE_LFRC    // 32.768 kHz RC oscillator

    ////////////////////////////////////////////////////////////////////////////////
    // Power

    #define PIN_EXT_VCC          (21)
    #define EXT_VCC              (PIN_EXT_VCC)

    #define BATTERY_PIN          (17)
    #define ADC_RESOLUTION       12

    ////////////////////////////////////////////////////////////////////////////////
    // Number of pins

    #define PINS_COUNT           (23)
    #define NUM_DIGITAL_PINS     (23)
    #define NUM_ANALOG_INPUTS    (3)
    #define NUM_ANALOG_OUTPUTS   (0)

    ////////////////////////////////////////////////////////////////////////////////
    // UART pin definition

    #define PIN_SERIAL1_TX       (1)
    #define PIN_SERIAL1_RX       (0)

    ////////////////////////////////////////////////////////////////////////////////
    // I2C pin definition

    #define WIRE_INTERFACES_COUNT 2

    #define PIN_WIRE_SDA         (6)
    #define PIN_WIRE_SCL         (7)
    #define PIN_WIRE1_SDA        (13)
    #define PIN_WIRE1_SCL        (14)

    ////////////////////////////////////////////////////////////////////////////////
    // SPI pin definition

    #define SPI_INTERFACES_COUNT 2

    #define PIN_SPI_SCK          (2)
    #define PIN_SPI_MISO         (3)
    #define PIN_SPI_MOSI         (4)

    #define PIN_SPI_NSS          (5)

    #define PIN_SPI1_SCK         (18)
    #define PIN_SPI1_MISO        (19)
    #define PIN_SPI1_MOSI        (20)

    ////////////////////////////////////////////////////////////////////////////////
    // Builtin LEDs

    #define PIN_LED              (22)
    #define LED_PIN              PIN_LED
    #define LED_BLUE             PIN_LED
    #define LED_BUILTIN          PIN_LED
    #define LED_STATE_ON         1

    ////////////////////////////////////////////////////////////////////////////////
    // Builtin buttons

    #define PIN_BUTTON1          (6)
    #define BUTTON_PIN           PIN_BUTTON1

#elif defined(XIAO_NRF52)
    #define USE_LFXO      // Board uses 32khz crystal for LF
    //#define USE_LFRC    // Board uses RC for LF

    #define PINS_COUNT              (33)
    #define NUM_DIGITAL_PINS        (33)
    #define NUM_ANALOG_INPUTS       (8)
    #define NUM_ANALOG_OUTPUTS      (0)

    // LEDs
    #define PIN_LED                 (LED_RED)
    #define LED_PWR                 (PINS_COUNT)
    #define PIN_NEOPIXEL            (PINS_COUNT)
    #define NEOPIXEL_NUM            (0)

    #define LED_BUILTIN             (PIN_LED)

    #define LED_RED                 (11)
    #define LED_GREEN               (13)
    #define LED_BLUE                (12)

    #define LED_STATE_ON            (0)     // State when LED is litted

    // Buttons
    #define PIN_BUTTON1             (PINS_COUNT)

    // Digital PINs
    static const uint8_t D0  = 0 ;
    static const uint8_t D1  = 1 ;
    static const uint8_t D2  = 2 ;
    static const uint8_t D3  = 3 ;
    static const uint8_t D4  = 4 ;
    static const uint8_t D5  = 5 ;
    static const uint8_t D6  = 6 ;
    static const uint8_t D7  = 7 ;
    static const uint8_t D8  = 8 ;
    static const uint8_t D9  = 9 ;
    static const uint8_t D10 = 10;

    #define VBAT_ENABLE             (14)    // Output LOW to enable reading of the BAT voltage.
                                            // https://wiki.seeedstudio.com/XIAO_BLE#q3-what-are-the-considerations-when-using-xiao-nrf52840-sense-for-battery-charging

    #define PIN_CHARGING_CURRENT    (22)    // Battery Charging current
                                            // https://wiki.seeedstudio.com/XIAO_BLE#battery-charging-current

    // Analog pins
    #define PIN_A0                  (0)
    #define PIN_A1                  (1)
    #define PIN_A2                  (2)
    #define PIN_A3                  (3)
    #define PIN_A4                  (4)
    #define PIN_A5                  (5)
    #define PIN_VBAT                (32)    // Read the BAT voltage.
                                            // https://wiki.seeedstudio.com/XIAO_BLE#q3-what-are-the-considerations-when-using-xiao-nrf52840-sense-for-battery-charging

    #define BAT_NOT_CHARGING        (23)    // LOW when charging

    #define AREF_VOLTAGE            (3.0)
    #define ADC_MULTIPLIER          (3.0F) // 1M, 512k divider bridge

    static const uint8_t A0  = PIN_A0;
    static const uint8_t A1  = PIN_A1;
    static const uint8_t A2  = PIN_A2;
    static const uint8_t A3  = PIN_A3;
    static const uint8_t A4  = PIN_A4;
    static const uint8_t A5  = PIN_A5;

    #define ADC_RESOLUTION          (12)

    // Other pins
    #define PIN_NFC1                (30)
    #define PIN_NFC2                (31)

    // Serial interfaces
    #define PIN_SERIAL1_RX          (7)
    #define PIN_SERIAL1_TX          (6)

    // SPI Interfaces
    #define SPI_INTERFACES_COUNT    (2)

    #define PIN_SPI_MISO            (9)
    #define PIN_SPI_MOSI            (10)
    #define PIN_SPI_SCK             (8)

    #define PIN_SPI1_MISO           (25)
    #define PIN_SPI1_MOSI           (26)
    #define PIN_SPI1_SCK            (29)

    // Lora SPI is on SPI0
    #define  P_LORA_SCLK            PIN_SPI_SCK
    #define  P_LORA_MISO            PIN_SPI_MISO
    #define  P_LORA_MOSI            PIN_SPI_MOSI

    // Wire Interfaces
    #define WIRE_INTERFACES_COUNT   (1)

    // #define PIN_WIRE_SDA            (17) // 4 and 5 are used for the sx1262 !
    // #define PIN_WIRE_SCL            (16) // use WIRE1_SDA

    static const uint8_t SDA = PIN_WIRE_SDA;
    static const uint8_t SCL = PIN_WIRE_SCL;

    //#define PIN_WIRE1_SDA           (17)
    //#define PIN_WIRE1_SCL           (16)
    #define PIN_LSM6DS3TR_C_POWER   (15)
    #define PIN_LSM6DS3TR_C_INT1    (18)

    // PDM Interfaces
    #define PIN_PDM_PWR	            (19)
    #define PIN_PDM_CLK	            (20)
    #define PIN_PDM_DIN	            (21)

    // QSPI Pins
    #define PIN_QSPI_SCK            (24)
    #define PIN_QSPI_CS             (25)
    #define PIN_QSPI_IO0            (26)
    #define PIN_QSPI_IO1            (27)
    #define PIN_QSPI_IO2            (28)
    #define PIN_QSPI_IO3            (29)

    // On-board QSPI Flash
    #define EXTERNAL_FLASH_DEVICES  (P25Q16H)
    #define EXTERNAL_FLASH_USE_QSPI
#endif
