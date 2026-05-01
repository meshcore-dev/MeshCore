#pragma once

#include "CustomSX1268.h"
#include "RadioLibWrappers.h"
#include "SX126xReset.h"

#ifndef USE_SX1268
#define USE_SX1268
#endif

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
  uint8_t getSpreadingFactor() const override { return ((CustomSX1268 *)_radio)->spreadingFactor; }

  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio); }

  void setRxBoostedGainMode(bool en) override {
    ((CustomSX1268 *)_radio)->setRxBoostedGainMode(en);
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomSX1268 *)_radio)->getRxBoostedGainMode();
  }
};
