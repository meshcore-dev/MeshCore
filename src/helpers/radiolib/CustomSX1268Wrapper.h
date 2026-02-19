#pragma once

#include "CustomSX1268.h"
#include "RadioLibWrappers.h"

class CustomSX1268Wrapper : public RadioLibWrapper {
public:
  CustomSX1268Wrapper(CustomSX1268& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { 
    return ((CustomSX1268 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1268 *)_radio)->getRSSI(false);
  }
  float getLastRSSI() const override { return ((CustomSX1268 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomSX1268 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1268 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }

  void doResetAGC() override {
    auto* radio = (CustomSX1268 *)_radio;
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
