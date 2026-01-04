#pragma once

#include "LinuxSX1262.h"
#include "RadioLibWrappers.h"

class LinuxSX1262Wrapper : public RadioLibWrapper {
public:
  LinuxSX1262Wrapper(LinuxSX1262& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { 
    return ((LinuxSX1262 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((LinuxSX1262 *)_radio)->getRSSI(false);
  }
  float getLastRSSI() const override { return ((LinuxSX1262 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((LinuxSX1262 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((LinuxSX1262 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
};
