#include "variant.h"

#include "Arduino.h"
#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
  0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
  14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
  27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
  40, 41, 42, 43, 44, 45, 46, 47
};

void initVariant()
{

}

void variant_shutdown()
{
  nrf_gpio_cfg_default(PIN_GPS_EN);
  nrf_gpio_cfg_default(PIN_GPS_PPS);
  nrf_gpio_cfg_default(PIN_GPS_RESET);
  nrf_gpio_cfg_default(PIN_GPS_STANDBY);
  nrf_gpio_cfg_default(GPS_RX_PIN);
  nrf_gpio_cfg_default(GPS_TX_PIN);
  pinMode(LORA_KCT8103L_EN, OUTPUT);
  digitalWrite(LORA_KCT8103L_EN, LOW);
  nrf_gpio_cfg_default(LORA_KCT8103L_TX_RX);
  nrf_gpio_cfg_default(RF_PA_DETECT_PIN);
  nrf_gpio_cfg_default(SX126X_CS);
  nrf_gpio_cfg_default(SX126X_DIO1);
  nrf_gpio_cfg_default(SX126X_BUSY);
  nrf_gpio_cfg_default(SX126X_RESET);
  nrf_gpio_cfg_default(PIN_SPI_MISO);
  nrf_gpio_cfg_default(PIN_SPI_MOSI);
  nrf_gpio_cfg_default(PIN_SPI_SCK);
  nrf_gpio_cfg_default(PIN_LED);
  detachInterrupt(PIN_GPS_PPS);
  detachInterrupt(PIN_BUTTON1);
}
