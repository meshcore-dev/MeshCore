#pragma once

#include "CustomLR1110.h"
#include "RadioLibWrappers.h"
#include "LR11x0Reset.h"

class CustomLR1110Wrapper : public RadioLibWrapper {
public:
  CustomLR1110Wrapper(CustomLR1110& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  void doResetAGC() override { lr11x0ResetAGC((LR11x0 *)_radio); }
  bool isReceivingPacket() override {
    return ((CustomLR1110 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR1110 *)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    _radio->setPreambleLength(16); // overcomes weird issues with small and big pkts
  }

  float getLastRSSI() const override { return ((CustomLR1110 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomLR1110 *)_radio)->getSNR(); }
  int16_t setRxBoostedGainMode(bool en) { return ((CustomLR1110 *)_radio)->setRxBoostedGainMode(en); };
};
