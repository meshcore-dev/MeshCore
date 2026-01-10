#include "variant.h"
#include "wiring_constants.h"
#include "wiring_digital.h"
#include "nrf.h"

const uint32_t g_ADigitalPinMap[] = {
    // D0 .. D10 - Peripheral control pins
    41, // D0  P1.09    GNSS_WAKEUP
    7,  // D1  P0.07     LORA_DIO1
    39, // D2  P1,07     LORA_RESET
    42, // D3  P1.10     LORA_BUSY
    46, // D4  P1.14 (A4/SDA) LORA_CS
    40, // D5  P1.08 (A5/SCL) LORA_SW
    27, // D6  P0.27 (UART_TX) GNSS_TX
    26, // D7  P0.26 (UART_RX) GNSS_RX
    30, // D8  P0.30 (SPI_SCK) LORA_SCK
    3,  // D9  P0.3 (SPI_MISO) LORA_MISO
    28, // D10 P0.28 (SPI_MOSI) LORA_MOSI

    // D11-D12 - LED outputs
    33, // D11 P1.1 User LED
    // Buzzzer
    32, // D12 P1.0 Buzzer

    // D13 - User input
    8, // D13 P0.08 User Button

    // D14-D15 - OLED
    6, // D14 P0.06 OLED SDA
    5, // D15 P0.05 OLED SCL

    // D16 - Battery voltage ADC input
    31, // D16 P0.31 VBAT_ADC
    // GROVE
    43, // D17 P0.00 GROVE SDA
    44, // D18 P0.01 GROVE SCL

    // FLASH
    21, // D19 P0.21 (QSPI_SCK)
    25, // D20 P0.25 (QSPI_CSN)
    20, // D21 P0.20 (QSPI_SIO_0 DI)
    24, // D22 P0.24 (QSPI_SIO_1 DO)
    22, // D23 P0.22 (QSPI_SIO_2 WP)
    23, // D24 P0.23 (QSPI_SIO_3 HOLD)

    // JOYSTICK
    36, // D25 TB_UP
    12, // D26 TB_DOWN
    11, // D27 TB_LEFT
    35, // D28 TB_RIGHT
    37, // D29 TB_PRESS

    // VBAT ENABLE
    4,  // D30 BAT_CTL

    // EINK
    13, // 31 SCK
    14, // 32 RST
    15, // 33 MOSI
    16, // 34 DC
    17, // 35 BUSY
    19, // 36 CS
    0xFF, // 37 MISO
};

void initVariant() {
    pinMode(PIN_QSPI_CS, OUTPUT);
    digitalWrite(PIN_QSPI_CS, HIGH);

    // VBAT_ENABLE
    pinMode(VBAT_ENABLE, OUTPUT);
    digitalWrite(VBAT_ENABLE, HIGH);

    // set LED pin as output and set it low
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    
    // set buzzer pin as output and set it low
    pinMode(12, OUTPUT);
    digitalWrite(12, LOW);
    pinMode(12, OUTPUT);
}
