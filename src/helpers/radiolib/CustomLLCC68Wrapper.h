#pragma once

#include "CustomLLCC68.h"
#include "RadioLibWrappers.h"
#include "SX126xReset.h"

class CustomLLCC68Wrapper : public RadioLibWrapper {
public:
  CustomLLCC68Wrapper(CustomLLCC68& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  bool applyParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    if (((CustomLLCC68 *)_radio)->setFrequency(freq) != RADIOLIB_ERR_NONE) return false;
    if (((CustomLLCC68 *)_radio)->setSpreadingFactor(sf) != RADIOLIB_ERR_NONE) return false;
    if (((CustomLLCC68 *)_radio)->setBandwidth(bw) != RADIOLIB_ERR_NONE) return false;
    if (((CustomLLCC68 *)_radio)->setCodingRate(cr) != RADIOLIB_ERR_NONE) return false;
    if (!updatePreamble(sf)) return false;
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, currentPreambleLength());
    ((CustomLLCC68 *)_radio)->setPreambleMillis(pm.preambleMillis);
    ((CustomLLCC68 *)_radio)->setMaxPayloadMillis(pm.payloadMillis);
    return true;
  }

  bool isReceivingPacket() override { 
    return ((CustomLLCC68 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomLLCC68 *)_radio)->getRSSI(false);
  }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomLLCC68 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  uint8_t getSpreadingFactor() const override { return ((CustomLLCC68 *)_radio)->spreadingFactor; }

  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio); }

  bool setRxBoostedGainMode(bool en) override {
    return ((CustomLLCC68 *)_radio)->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomLLCC68 *)_radio)->getRxBoostedGainMode();
  }
};
