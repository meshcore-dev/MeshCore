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

  bool applyParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    if (((CustomSX1268 *)_radio)->setFrequency(freq) != RADIOLIB_ERR_NONE) return false;
    if (((CustomSX1268 *)_radio)->setSpreadingFactor(sf) != RADIOLIB_ERR_NONE) return false;
    if (((CustomSX1268 *)_radio)->setBandwidth(bw) != RADIOLIB_ERR_NONE) return false;
    if (((CustomSX1268 *)_radio)->setCodingRate(cr) != RADIOLIB_ERR_NONE) return false;
    if (!updatePreamble(sf)) return false;
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, currentPreambleLength());
    ((CustomSX1268 *)_radio)->setPreambleMillis(pm.preambleMillis);
    ((CustomSX1268 *)_radio)->setMaxPayloadMillis(pm.payloadMillis);
    return true;
  }

  bool isReceivingPacket() override { 
    return ((CustomSX1268 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1268 *)_radio)->getRSSI(false);
  }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1268 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  uint8_t getSpreadingFactor() const override { return ((CustomSX1268 *)_radio)->spreadingFactor; }

  bool setRxBoostedGainMode(bool en) override {
    return ((CustomSX1268 *)_radio)->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomSX1268 *)_radio)->getRxBoostedGainMode();
  }

  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode()); }
};
