#include "T096Board.h"

#include <Arduino.h>
#include <Wire.h>

#ifdef NRF52_POWER_MANAGEMENT
// Static configuration for power management
// Values come from variant.h defines
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void T096Board::initiateShutdown(uint8_t reason) {
#if ENV_INCLUDE_GPS == 1
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, !PIN_GPS_EN_ACTIVE);
#endif
  variant_shutdown();

  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);
  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, enable_lpcomp ? HIGH : LOW);

  if (enable_lpcomp) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void T096Board::begin() {
  NRF52Board::begin();

#ifdef NRF52_POWER_MANAGEMENT
  // Boot voltage protection check (may not return if voltage too low)
  checkBootVoltage(&power_config);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, LOW);

  periph_power.begin();
  loRaFEMControl.init();
  delay(1);
}

void T096Board::onBeforeTransmit() {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
    loRaFEMControl.setTxModeEnable();
}

void T096Board::onAfterTransmit() {
    digitalWrite(P_LORA_TX_LED, LOW);   //turn TX LED off
    loRaFEMControl.setRxModeEnable();
}

uint16_t T096Board::getBattMilliVolts() {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    pinMode(PIN_VBAT_READ, INPUT);
    pinMode(PIN_BAT_CTL, OUTPUT);
    digitalWrite(PIN_BAT_CTL, 1);

    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    digitalWrite(PIN_BAT_CTL, 0);

    return (uint16_t)((float)adcvalue * MV_LSB * 4.9);
}
void T096Board::variant_shutdown() {
 nrf_gpio_cfg_default(PIN_VEXT_EN);
    nrf_gpio_cfg_default(PIN_TFT_CS);
    nrf_gpio_cfg_default(PIN_TFT_DC);
    nrf_gpio_cfg_default(PIN_TFT_SDA);
    nrf_gpio_cfg_default(PIN_TFT_SCL);
    nrf_gpio_cfg_default(PIN_TFT_RST);
    nrf_gpio_cfg_default(PIN_TFT_LEDA_CTL);

    nrf_gpio_cfg_default(PIN_LED);

    nrf_gpio_cfg_default(P_LORA_KCT8103L_PA_CSD);
    nrf_gpio_cfg_default(P_LORA_KCT8103L_PA_CTX);
    pinMode(P_LORA_PA_POWER, OUTPUT);
    digitalWrite(P_LORA_PA_POWER, LOW);

    digitalWrite(PIN_BAT_CTL, LOW);
    nrf_gpio_cfg_default(LORA_CS);
    nrf_gpio_cfg_default(SX126X_DIO1);
    nrf_gpio_cfg_default(SX126X_BUSY);
    nrf_gpio_cfg_default(SX126X_RESET);

    nrf_gpio_cfg_default(PIN_SPI_MISO);
    nrf_gpio_cfg_default(PIN_SPI_MOSI);
    nrf_gpio_cfg_default(PIN_SPI_SCK);

    // nrf_gpio_cfg_default(PIN_GPS_PPS);
    nrf_gpio_cfg_default(PIN_GPS_RESET);
    nrf_gpio_cfg_default(PIN_GPS_EN);
    nrf_gpio_cfg_default(PIN_GPS_RX);
    nrf_gpio_cfg_default(PIN_GPS_TX);
}

void T096Board::powerOff() {
#if ENV_INCLUDE_GPS == 1
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, !PIN_GPS_EN_ACTIVE);
#endif
    loRaFEMControl.setSleepModeEnable();
    variant_shutdown();
    sd_power_system_off();
}

const char* T096Board::getManufacturerName() const {
  return "Heltec T096";
}