#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include "nrf.h"

const uint32_t g_ADigitalPinMap[] = {
    // D0 .. D10 - Peripheral control pins
    2,  // D0  P0.02 (A0)    GNSS_WAKEUP
    3,  // D1  P0.03 (A1)    LORA_DIO1
    28, // D2  P0.28 (A2)    LORA_RESET
    29, // D3  P0.29 (A3)    LORA_BUSY
    4,  // D4  P0.04 (A4/SDA) LORA_CS
    5,  // D5  P0.05 (A5/SCL) LORA_SW
    43, // D6  P1.11 (UART_TX) GNSS_TX
    44, // D7  P1.12 (UART_RX) GNSS_RX
    45, // D8  P1.13 (SPI_SCK) LORA_SCK
    46, // D9  P1.14 (SPI_MISO) LORA_MISO
    47, // D10 P1.15 (SPI_MOSI) LORA_MOSI

    // D11-D12 - LED outputs
    15, // D11 P0.15 User LED
    19, // D12 P0.19 Breathing LED

    // D13 - User input
    33, // D13 P1.01 User Button

    // D14-D15 - Grove/NFC interface
    9,  // D14 P0.09 NFC1/GROVE_D1
    10, // D15 P0.10 NFC2/GROVE_D0

    // D16 - Power management
    // 31, // D16 P0.31 VBAT_ADC (Battery voltage)
    31, // D16 P0.31 VBAT_ADC (Battery voltage)
    // D17 - GNSS control
    35, // D17 P1.03 GNSS_RESET

    37, // D18 P1.05 GNSS_ENABLE
    14, // D19 P0.14 BAT_READ
    39, // D20 P1.07 USER_BUTTON

    //
    21, // D21 P0.21 (QSPI_SCK)
    25, // D22 P0.25 (QSPI_CSN)
    20, // D23 P0.20 (QSPI_SIO_0 DI)
    24, // D24 P0.24 (QSPI_SIO_1 DO)
    22, // D25 P0.22 (QSPI_SIO_2 WP)
    23, // D26 P0.23 (QSPI_SIO_3 HOLD)
};

void initVariant() {
    pinMode(GPS_EN, OUTPUT);
    digitalWrite(GPS_EN, LOW);

    pinMode(BATTERY_PIN, INPUT);
    pinMode(VBAT_ENABLE, OUTPUT);
    digitalWrite(VBAT_ENABLE, LOW);

    pinMode(PIN_QSPI_CS, OUTPUT);
    digitalWrite(PIN_QSPI_CS, HIGH);

    pinMode(LED_GREEN, OUTPUT);
    digitalWrite(LED_GREEN, LOW);

    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_BLUE, LOW);

    /* disable gps until we actually support it.
    pinMode(GPS_EN, OUTPUT);
    digitalWrite(GPS_EN, HIGH);
    */
}
