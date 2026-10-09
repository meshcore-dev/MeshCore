#include "StationG3Board.h"

void StationG3Board::powerOff() {
  loRaFEMControl.setSleepModeEnable();
#ifdef P_PA1_EN
  rtc_gpio_hold_en((gpio_num_t)P_PA1_EN);
#endif

#ifdef P_PRIMARY_LNA_EN
  rtc_gpio_hold_en((gpio_num_t)P_PRIMARY_LNA_EN);
#endif

  ESP32Board::powerOff();
}

bool StationG3Board::setLoRaFemLnaEnabled(bool enable) {
  if (!loRaFEMControl.canControlLNA()) {
    return false;
  }
  loRaFEMControl.setLNAEnable(enable);
  return true;
}

bool StationG3Board::canControlLoRaFemLna() const {
  return loRaFEMControl.canControlLNA();
}

bool StationG3Board::isLoRaFemLnaEnabled() const {
  return loRaFEMControl.isLNAEnabled();
}

bool StationG3Board::setLoRaFemPaGainEnabled(bool enable) {
  if (!loRaFEMControl.canControlPAGain()) {
    return false;
  }
  loRaFEMControl.setPAGainEnable(enable);
  return true;
}

bool StationG3Board::canControlLoRaFemPaGain() const {
  return loRaFEMControl.canControlPAGain();
}

bool StationG3Board::isLoRaFemPaGainEnabled() const {
  return loRaFEMControl.isPAGainEnabled();
}
