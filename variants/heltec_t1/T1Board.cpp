#include "T1Board.h"

#include <Arduino.h>
#include <Wire.h>

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void T1Board::initiateShutdown(uint8_t reason) {
  variant_shutdown();

  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);
  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, enable_lpcomp ? ADC_CTRL_ENABLED : !ADC_CTRL_ENABLED);

  if (enable_lpcomp) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif

void T1Board::begin() {
  NRF52Board::begin();

#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif
  Wire.begin();

  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, !LED_STATE_ON);

  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, !PIN_GPS_EN_ACTIVE);

  pinMode(PIN_SENSOR_EN, OUTPUT);
  digitalWrite(PIN_SENSOR_EN, PIN_SENSOR_EN_ACTIVE);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BUZZER_VOLTAGE_MULTIPLIER_1, OUTPUT);
  pinMode(PIN_BUZZER_VOLTAGE_MULTIPLIER_2, OUTPUT);
  digitalWrite(PIN_BUZZER_VOLTAGE_MULTIPLIER_1, HIGH);
  digitalWrite(PIN_BUZZER_VOLTAGE_MULTIPLIER_2, HIGH);

  periph_power.begin();
  delay(1);
}

void T1Board::onBeforeTransmit() {
  digitalWrite(P_LORA_TX_LED, LED_STATE_ON);
}

void T1Board::onAfterTransmit() {
  digitalWrite(P_LORA_TX_LED, !LED_STATE_ON);
}

uint16_t T1Board::getBattMilliVolts() {
  analogReadResolution(12);
  analogReference(VBAT_AR_INTERNAL);
  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, ADC_CTRL_ENABLED);

  delay(10);
  int adcvalue = analogRead(PIN_VBAT_READ);
  digitalWrite(PIN_BAT_CTL, !ADC_CTRL_ENABLED);

  return (uint16_t)((float)adcvalue * MV_LSB * ADC_MULTIPLIER);
}

void T1Board::variant_shutdown() {
  nrf_gpio_cfg_default(PIN_BUZZER_VOLTAGE_MULTIPLIER_1);
  nrf_gpio_cfg_default(PIN_BUZZER_VOLTAGE_MULTIPLIER_2);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_SENSOR_EN, OUTPUT);
  digitalWrite(PIN_SENSOR_EN, !PIN_SENSOR_EN_ACTIVE);

  pinMode(PIN_LED1, OUTPUT);
  digitalWrite(PIN_LED1, !LED_STATE_ON);

  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, !ADC_CTRL_ENABLED);
}

void T1Board::powerOff() {
  variant_shutdown();
  NRF52Board::powerOff();
}

const char* T1Board::getManufacturerName() const {
  return "Heltec T1";
}
