#pragma once

#include <RadioLib.h>

// Full receiver reset for all SX126x-family chips (SX1262, SX1268, LLCC68, STM32WLx).
// RadioLib 7.7.0+ provides resetAGC(): warm sleep powers down analog, Calibrate(0x7F)
// refreshes ADC/PLL/image calibration, image is re-calibrated for the operating
// frequency, and DIO2 RF switch / RX boosted gain are re-applied automatically.
inline void sx126xResetAGC(SX126x* radio, bool rx_boost_gain) {
  radio->resetAGC();

#ifdef SX126X_RX_BOOSTED_GAIN
  // resetAGC() only restores boosted gain when it was enabled, so apply the
  // caller's current (user/flash) setting explicitly.
  radio->setRxBoostedGainMode(rx_boost_gain);
#endif
#ifdef SX126X_REGISTER_PATCH
  // for improved RX with Heltec v4 — calibration may reset this
  uint8_t r_data = 0;
  radio->readRegister(0x8B5, &r_data, 1);
  r_data |= 0x01;
  radio->writeRegister(0x8B5, &r_data, 1);
#endif
}
