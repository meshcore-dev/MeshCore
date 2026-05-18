#pragma once

#include "CustomLLCC68.h"
#include "RadioLibWrappers.h"
#include "SX126xReset.h"

class CustomLLCC68Wrapper : public RadioLibWrapper {
public:
  CustomLLCC68Wrapper(CustomLLCC68& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { 
    return ((CustomLLCC68 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomLLCC68 *)_radio)->getRSSI(false);
  }
  float getLastRSSI() const override { return ((CustomLLCC68 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomLLCC68 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomLLCC68 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  bool setCodingRate(uint8_t cr) override {
    idle();
    int err = ((CustomLLCC68 *)_radio)->setCodingRate(cr);
    if (err != RADIOLIB_ERR_NONE) {
      MESH_DEBUG_PRINTLN("CustomLLCC68Wrapper: error: setCodingRate(%d)=%d", (uint32_t)cr, err);
      return false;
    }
    return true;
  }

  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio); }

  void setRxBoostedGainMode(bool en) override {
    ((CustomLLCC68 *)_radio)->setRxBoostedGainMode(en);
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomLLCC68 *)_radio)->getRxBoostedGainMode();
  }
};
