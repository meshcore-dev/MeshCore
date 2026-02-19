#pragma once

#include "CustomSX1262.h"
#include "RadioLibWrappers.h"

class CustomSX1262Wrapper : public RadioLibWrapper {
public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { 
    return ((CustomSX1262 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1262 *)_radio)->getRSSI(false);
  }
  float getLastRSSI() const override { return ((CustomSX1262 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomSX1262 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1262 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  virtual void powerOff() override {
    ((CustomSX1262 *)_radio)->sleep(false);
  }

  void doResetAGC() override {
    auto* radio = (CustomSX1262 *)_radio;
    // Warm sleep powers down analog frontend (resets AGC gain state)
    radio->sleep(true);
    // Wake to STDBY_RC for calibration
    radio->standby(RADIOLIB_SX126X_STANDBY_RC, true);
    // Recalibrate all blocks (ADC, PLL, image, oscillators)
    uint8_t calData = RADIOLIB_SX126X_CALIBRATE_ALL;
    radio->mod->SPIwriteStream(RADIOLIB_SX126X_CMD_CALIBRATE, &calData, 1, true, false);
    radio->mod->hal->delay(5);
    uint32_t start = millis();
    while (radio->mod->hal->digitalRead(radio->mod->getGpio())) {
      if (millis() - start > 50) break;
      radio->mod->hal->yield();
    }
    // Re-apply RX settings that calibration may reset
#ifdef SX126X_DIO2_AS_RF_SWITCH
    radio->setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
#endif
#ifdef SX126X_RX_BOOSTED_GAIN
    radio->setRxBoostedGainMode(SX126X_RX_BOOSTED_GAIN);
#endif
#ifdef SX126X_REGISTER_PATCH
    uint8_t r_data = 0;
    radio->readRegister(0x8B5, &r_data, 1);
    r_data |= 0x01;
    radio->writeRegister(0x8B5, &r_data, 1);
#endif
  }
};
