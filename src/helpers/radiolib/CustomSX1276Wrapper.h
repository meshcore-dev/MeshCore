#pragma once

#include "CustomSX1276.h"
#include "RadioLibWrappers.h"

class CustomSX1276Wrapper : public RadioLibWrapper {
public:
  CustomSX1276Wrapper(CustomSX1276& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { 
    return ((CustomSX1276 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1276 *)_radio)->getRSSI(false);
  }
  float getLastRSSI() const override { return ((CustomSX1276 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomSX1276 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    // Use the configured spreading factor macro
    int sf = LORA_SF;
    return packetScoreInt(snr, sf, packet_len);
  }
};
