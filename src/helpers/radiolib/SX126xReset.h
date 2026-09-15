#pragma once

#include <RadioLib.h>

// Raw SX126x GetStatus read. RadioLib's SX126x::getStatus() passes numBytes = 0 to
// SPIreadStream(), so the status byte is never copied out of the transfer buffer and the
// function returns 0 unconditionally (same in RadioLib master) — the chip-mode bits read
// as "not RX" on a perfectly healthy radio. The chip clocks its status byte on MISO
// during the byte AFTER the opcode (RadioLib's own SX126x SPI config parses buffIn[1],
// statusPos = 1), so a plain [GetStatus, NOP] transfer taking the second byte is the
// correct read. Hardware-verified (agessaman, PR #2933 review): returns 0x52 (chip mode
// RX) on a healthy receiver in 2874/2874 polls where the broken method returns 0x00.
// Works for all SX126x-family chips (SX1262, SX1268, LLCC68, STM32WLx).
inline uint8_t sx126xGetStatus(SX126x* radio) {
  uint8_t buf[2] = { RADIOLIB_SX126X_CMD_GET_STATUS, RADIOLIB_SX126X_CMD_NOP };
  radio->mod->hal->spiBeginTransaction();
  radio->mod->hal->digitalWrite(radio->mod->csPin, radio->mod->hal->GpioLevelLow);
  radio->mod->hal->spiTransfer(buf, 2, buf);
  radio->mod->hal->digitalWrite(radio->mod->csPin, radio->mod->hal->GpioLevelHigh);
  radio->mod->hal->spiEndTransaction();
  return buf[1];
}

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

  // Calibrate(0x7F) defaults image calibration to 902-928MHz band.
  // Re-calibrate for the actual operating frequency.
  radio->calibrateImage(radio->freqMHz);

#ifdef SX126X_DIO2_AS_RF_SWITCH
  radio->setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
#endif
#ifdef SX126X_RX_BOOSTED_GAIN
  radio->setRxBoostedGainMode(rx_boost_gain);
#endif
#ifdef SX126X_REGISTER_PATCH
  uint8_t r_data = 0;
  radio->readRegister(0x8B5, &r_data, 1);
  r_data |= 0x01;
  radio->writeRegister(0x8B5, &r_data, 1);
#endif
}
