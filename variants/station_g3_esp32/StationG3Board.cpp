#include "StationG3Board.h"

void StationG3Board::powerOff() {
#ifdef P_PA1_EN
  setPAModeHigh(false);
  rtc_gpio_hold_en((gpio_num_t)P_PA1_EN);
#endif

#ifdef P_PRIMARY_LNA_EN
  setPrimaryLNAControl(true);
  rtc_gpio_hold_en((gpio_num_t)P_PRIMARY_LNA_EN);
#endif

  ESP32Board::powerOff();
}
