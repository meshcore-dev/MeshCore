#include "variant.h"

#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[PINS_COUNT + 1] = {
  0,   // P0.00
  1,   // P0.01
  2,   // P0.02
  3,   // P0.03
  4,   // P0.04
  5,   // P0.05
  6,   // P0.06
  7,   // P0.07
  8,   // P0.08
  9,   // P0.09
  10,  // P0.10
  11,  // P0.11
  12,  // P0.12
  13,  // P0.13, PIN_SERIAL1_TX
  14,  // P0.14, PIN_SERIAL1_RX
  15,  // P0.15, PIN_SERIAL2_RX
  16,  // P0.16, PIN_WIRE_SCL
  17,  // P0.17, PIN_SERIAL2_TX
  18,  // P0.18
  19,  // P0.19
  20,  // P0.20
  21,  // P0.21, PIN_WIRE_SDA
  22,  // P0.22
  23,  // P0.23
  24,  // P0.24,
  25,  // P0.25,
  26,  // P0.26,
  27,  // P0.27,
  28,  // P0.28
  29,  // P0.29,
  30,  // P0.30
  31,  // P0.31, BATTERY_PIN
  32,  // P1.00
  33,  // P1.01, LORA_DIO_1
  34,  // P1.02
  35,  // P1.03,
  36,  // P1.04
  37,  // P1.05, LR1110_EN
  38,  // P1.06,
  39,  // P1.07,
  40,  // P1.08, PIN_SPI_MISO
  41,  // P1.09, PIN_SPI_MOSI
  42,  // P1.10, LORA_RESET
  43,  // P1.11, GPS_EN
  44,  // P1.12, GPS_SLEEP_INT
  45,  // P1.13
  46,  // P1.14, GPS_RESETB
  47,  // P1.15, PIN_GPS_RESET
  255, // NRFX_SPIM_PIN_NOT_USED
};

void initVariant() {
  pinMode(BATTERY_PIN, INPUT);
  pinMode(PIN_BUTTON1, INPUT);

  // pinMode(PIN_3V3_EN, OUTPUT);
  // pinMode(PIN_3V3_ACC_EN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(P_LORA_TX_LED, OUTPUT);

  digitalWrite(LED_PIN, HIGH);
  digitalWrite(P_LORA_TX_LED, LOW);
}
