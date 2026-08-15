#include "HeltecTowerV2Board.h"

#include <Arduino.h>
#include <Wire.h>

extern void variant_shutdown();

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void HeltecTowerV2Board::initiateShutdown(uint8_t reason) {
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, !PIN_GPS_EN_ACTIVE);
  pinMode(PIN_GPS_STANDBY, OUTPUT);
  digitalWrite(PIN_GPS_STANDBY, LOW);
  pinMode(PIN_GPS_RESET, OUTPUT);
  digitalWrite(PIN_GPS_RESET, GPS_RESET_MODE);
  loRaFEMControl.setSleepModeEnable();

  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);
  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, enable_lpcomp ? HIGH : LOW);

  if (enable_lpcomp) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  variant_shutdown();
  enterSystemOff(reason);
}
#endif

void HeltecTowerV2Board::begin() {
  NRF52Board::begin();

  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, !LED_STATE_ON);

  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, LOW);

#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
#endif

  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
  Wire.begin();

  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, !PIN_GPS_EN_ACTIVE);
  pinMode(PIN_GPS_RESET, OUTPUT);
  digitalWrite(PIN_GPS_RESET, GPS_RESET_MODE);
  pinMode(PIN_GPS_STANDBY, OUTPUT);
  digitalWrite(PIN_GPS_STANDBY, HIGH);
  loRaFEMControl.init();
}

void HeltecTowerV2Board::onBeforeTransmit() {
  digitalWrite(P_LORA_TX_LED, LED_STATE_ON);
  loRaFEMControl.setTxModeEnable();
}

void HeltecTowerV2Board::onAfterTransmit() {
  digitalWrite(P_LORA_TX_LED, !LED_STATE_ON);
  loRaFEMControl.setRxModeEnable();
}

uint16_t HeltecTowerV2Board::getBattMilliVolts() {
  analogReadResolution(12);
  analogReference(VBAT_AR_INTERNAL);
  pinMode(PIN_VBAT_READ, INPUT);
  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, HIGH);

  delay(10);
  int adcvalue = analogRead(PIN_VBAT_READ);
  digitalWrite(PIN_BAT_CTL, LOW);

  return (uint16_t)((float)adcvalue * MV_LSB * ADC_MULTIPLIER);
}

const char* HeltecTowerV2Board::getManufacturerName() const {
  return "Heltec Tower V2";
}

void HeltecTowerV2Board::powerOff() {
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, !PIN_GPS_EN_ACTIVE);
  pinMode(PIN_GPS_STANDBY, OUTPUT);
  digitalWrite(PIN_GPS_STANDBY, LOW);
  pinMode(PIN_GPS_RESET, OUTPUT);
  digitalWrite(PIN_GPS_RESET, GPS_RESET_MODE);
  loRaFEMControl.setSleepModeEnable();
  pinMode(PIN_BAT_CTL, OUTPUT);
  digitalWrite(PIN_BAT_CTL, LOW);
  variant_shutdown();
  sd_power_system_off();
}
