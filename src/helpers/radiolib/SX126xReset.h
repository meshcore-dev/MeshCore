#pragma once

#include <RadioLib.h>

// Full receiver reset for all SX126x-family chips (SX1262, SX1268, LLCC68, STM32WLx).
// Warm sleep powers down analog, Calibrate(0x7F) refreshes ADC/PLL/image calibration,
// then re-applies RX settings that calibration may reset.
inline void sx126xResetAGC(SX126x* radio, bool rx_boost_gain) {
  radio->sleep(true);
  radio->standby(RADIOLIB_SX126X_STANDBY_RC, true);

  uint8_t calData = RADIOLIB_SX126X_CALIBRATE_ALL;
  radio->mod->SPIwriteStream(RADIOLIB_SX126X_CMD_CALIBRATE, &calData, 1, true, false);
  radio->mod->hal->delay(5);
  uint32_t start = millis();
  while (radio->mod->hal->digitalRead(radio->mod->getGpio())) {
    if (millis() - start > 50) break;
    radio->mod->hal->yield();
  }
  // calibration not finished: skip the register writes below, the next reset retries
  if (radio->mod->hal->digitalRead(radio->mod->getGpio())) return;

  // Calibrate(0x7F) defaults image calibration to 902-928MHz band.
  // Re-calibrate for the actual operating frequency.
  radio->calibrateImage(radio->freqMHz);
  // image calibration keeps running after BUSY drops; a register write in that
  // window can fail and stall the chip (same 50 ms wait as in Meshtastic)
  radio->mod->hal->delay(50);

#ifdef SX126X_DIO2_AS_RF_SWITCH
  radio->setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
#endif
  // always reapply: the gain may have been changed at runtime even when no build default is set
  radio->setRxBoostedGainMode(rx_boost_gain);
#ifdef SX126X_REGISTER_PATCH
  uint8_t r_data = 0;
  radio->readRegister(0x8B5, &r_data, 1);
  r_data |= 0x01;
  radio->writeRegister(0x8B5, &r_data, 1);
#endif
}
