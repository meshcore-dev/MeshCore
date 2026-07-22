#include "variant.h"
#include "Arduino.h"
#include "nrf.h"
#include <Wire.h>
#include "wiring_constants.h"
#include "wiring_digital.h"

const uint32_t g_ADigitalPinMap[] = {
    0xff, 0xff, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16,   17,   18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32,   33,   34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

static void resetOptionalPin(int pin)
{
  if (pin >= 0) nrf_gpio_cfg_default(pin);
}

void initVariant()
{
  pinMode(PIN_BUTTON1, INPUT_PULLUP);

  pinMode(RADIOCORE_FEM_EN, OUTPUT);
  digitalWrite(RADIOCORE_FEM_EN, HIGH);

  pinMode(RADIOCORE_VFEM_CTRL, OUTPUT);
  digitalWrite(RADIOCORE_VFEM_CTRL, HIGH);

  pinMode(PIN_ADC_CTRL, OUTPUT);
  digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);

#ifdef PIN_BUZZER
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
#endif

  pinMode(PIN_TFT_EN, OUTPUT);
  digitalWrite(PIN_TFT_EN, !PIN_TFT_EN_ACTIVE);
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
}

void variant_shutdown()
{
  Wire.end();

  digitalWrite(RADIOCORE_FEM_EN, LOW);
  digitalWrite(RADIOCORE_VFEM_CTRL, LOW);
  digitalWrite(PIN_ADC_CTRL, !ADC_CTRL_ENABLED);
#ifdef PIN_BUZZER
  digitalWrite(PIN_BUZZER, LOW);
#endif
  digitalWrite(PIN_TFT_BL, !PIN_TFT_BL_ACTIVE);
  digitalWrite(PIN_TFT_EN, !PIN_TFT_EN_ACTIVE);

  nrf_gpio_cfg_default(PIN_BUTTON1);
  nrf_gpio_cfg_default(PIN_ADC_CTRL);
#ifdef PIN_BUZZER
  nrf_gpio_cfg_default(PIN_BUZZER);
#endif

  nrf_gpio_cfg_default(PIN_TFT_BL);
  nrf_gpio_cfg_default(PIN_TFT_EN);
  nrf_gpio_cfg_default(PIN_TFT_CS);
  nrf_gpio_cfg_default(PIN_TFT_DC);
  nrf_gpio_cfg_default(PIN_TFT_MOSI);
  nrf_gpio_cfg_default(PIN_TFT_RST);

  nrf_gpio_cfg_default(PIN_SPI_MISO);
  nrf_gpio_cfg_default(PIN_SPI_MOSI);
  nrf_gpio_cfg_default(PIN_SPI_SCK);

  resetOptionalPin(PIN_SPI1_MISO);
  nrf_gpio_cfg_default(PIN_SPI1_MOSI);
  nrf_gpio_cfg_default(PIN_SPI1_SCK);

  nrf_gpio_cfg_default(PIN_GPS_EN);
  nrf_gpio_cfg_default(PIN_GPS_RESET);
  nrf_gpio_cfg_default(PIN_GPS_PPS);
  nrf_gpio_cfg_default(PIN_GPS_RX);
  nrf_gpio_cfg_default(PIN_GPS_TX);

  nrf_gpio_cfg_default(SENSOR_POWER_CTRL_PIN);
  nrf_gpio_cfg_default(SENSOR_RST_PIN);
  nrf_gpio_cfg_default(SENSOR_INT);
  nrf_gpio_cfg_default(PIN_BOARD_SDA);
  nrf_gpio_cfg_default(PIN_BOARD_SCL);

  nrf_gpio_cfg_default(RADIOCORE_FEM_EN);
  nrf_gpio_cfg_default(RADIOCORE_VFEM_CTRL);
}
