#pragma once

#include <RadioLib.h>

// Full receiver reset for LR11x0-family chips (LR1110, LR1120, LR1121).
// Warm sleep powers down analog, calibrate(0x3F) refreshes all calibration blocks,
// then re-applies RX settings that calibration may reset.
inline void lr11x0ResetAGC(LR11x0* radio) {
  radio->sleep(true, 0);
  radio->standby(RADIOLIB_LR11X0_STANDBY_RC, true);

  radio->calibrate(RADIOLIB_LR11X0_CALIBRATE_ALL);

#ifdef RX_BOOSTED_GAIN
  radio->setRxBoostedGainMode(RX_BOOSTED_GAIN);
#endif
}
