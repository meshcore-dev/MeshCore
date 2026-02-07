#pragma once

#include "CustomLR1121.h"
#include "RadioLibWrappers.h"

class CustomLR1121Wrapper : public RadioLibWrapper {
public:
  CustomLR1121Wrapper(CustomLR1121& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  bool isReceivingPacket() override {
    return ((CustomLR1121*)_radio)->isReceiving();
  }

  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR1121*)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    ((CustomLR1121*)_radio)->setPreambleLength(16);
  }

  float getLastRSSI() const override { return ((CustomLR1121*)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomLR1121*)_radio)->getSNR(); }
  int16_t setRxBoostedGainMode(bool en) { return ((CustomLR1121*)_radio)->setRxBoostedGainMode(en); }
};
