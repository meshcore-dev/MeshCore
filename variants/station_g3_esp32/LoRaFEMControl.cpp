#include "LoRaFEMControl.h"

#include <Arduino.h>
#include <driver/rtc_io.h>

void LoRaFEMControl::init() {
#ifdef P_PA1_EN
  rtc_gpio_hold_dis((gpio_num_t)P_PA1_EN);
  pinMode(P_PA1_EN, OUTPUT);
  setPAGainEnable(pa_gain_enabled);
#endif

#ifdef P_PRIMARY_LNA_EN
  rtc_gpio_hold_dis((gpio_num_t)P_PRIMARY_LNA_EN);
  pinMode(P_PRIMARY_LNA_EN, OUTPUT);
  setRxModeEnable();
#endif
}

void LoRaFEMControl::setSleepModeEnable() {
#ifdef P_PA1_EN
  // PA PL1 low/open selects the lower of the two hardware-jumper-selected levels.
  digitalWrite(P_PA1_EN, !P_PA1_EN_ACTIVE);
#endif
#ifdef P_PRIMARY_LNA_EN
  // Preserve the existing Station G3 power-off state.
  digitalWrite(P_PRIMARY_LNA_EN, P_PRIMARY_LNA_EN_ACTIVE);
#endif
}

void LoRaFEMControl::setTxModeEnable() {
#ifdef P_PRIMARY_LNA_EN
  digitalWrite(P_PRIMARY_LNA_EN, !P_PRIMARY_LNA_EN_ACTIVE);
#endif
}

void LoRaFEMControl::setRxModeEnable() {
#ifdef P_PRIMARY_LNA_EN
  digitalWrite(P_PRIMARY_LNA_EN, lna_enabled ? P_PRIMARY_LNA_EN_ACTIVE : !P_PRIMARY_LNA_EN_ACTIVE);
#endif
}

void LoRaFEMControl::setLNAEnable(bool enabled) {
  lna_enabled = enabled;
  setRxModeEnable();
}

void LoRaFEMControl::setPAGainEnable(bool enabled) {
  pa_gain_enabled = enabled;
#ifdef P_PA1_EN
  digitalWrite(P_PA1_EN, enabled ? P_PA1_EN_ACTIVE : !P_PA1_EN_ACTIVE);
#endif
}

bool LoRaFEMControl::canControlLNA() const {
#ifdef P_PRIMARY_LNA_EN
  return true;
#else
  return false;
#endif
}

bool LoRaFEMControl::canControlPAGain() const {
#ifdef P_PA1_EN
  return true;
#else
  return false;
#endif
}
