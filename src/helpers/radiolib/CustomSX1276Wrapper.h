#pragma once

#include "CustomSX1276.h"
#include "RadioLibWrappers.h"

#ifndef USE_SX1276
#define USE_SX1276
#endif

class CustomSX1276Wrapper : public RadioLibWrapper {
public:
  CustomSX1276Wrapper(CustomSX1276& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  bool applyParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    if (((CustomSX1276 *)_radio)->setFrequency(freq) != RADIOLIB_ERR_NONE) return false;
    if (((CustomSX1276 *)_radio)->setSpreadingFactor(sf) != RADIOLIB_ERR_NONE) return false;
    if (((CustomSX1276 *)_radio)->setBandwidth(bw) != RADIOLIB_ERR_NONE) return false;
    if (((CustomSX1276 *)_radio)->setCodingRate(cr) != RADIOLIB_ERR_NONE) return false;
    if (!updatePreamble(sf)) return false;
    return true;
  }

  bool isReceivingPacket() override { 
    return ((CustomSX1276 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1276 *)_radio)->getRSSI(false);
  }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1276 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  uint8_t getSpreadingFactor() const override { return ((CustomSX1276 *)_radio)->spreadingFactor; }
};
