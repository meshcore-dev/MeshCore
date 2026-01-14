#pragma once

#include "CustomSX1262.h"
#include "RadioLibWrappers.h"

// RSSI_OFFSET is used to compensate for external LNA gain (e.g.Heltec V4)
// Set to a negative value equal to the LNA gain to get true RSSI values
#ifndef RSSI_OFFSET
  #define RSSI_OFFSET 0
#endif

class CustomSX1262Wrapper : public RadioLibWrapper {
public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { 
    return ((CustomSX1262 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1262 *)_radio)->getRSSI(false) + RSSI_OFFSET;
  }
  float getLastRSSI() const override { return ((CustomSX1262 *)_radio)->getRSSI() + RSSI_OFFSET; }
  float getLastSNR() const override { return ((CustomSX1262 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1262 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  virtual void powerOff() override {
    ((CustomSX1262 *)_radio)->sleep(false);
  }
};
