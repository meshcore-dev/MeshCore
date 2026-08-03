#pragma once

#include "CustomSTM32WLx.h"
#include "RadioLibWrappers.h"
#include "SX126xReset.h"
#include <math.h>

class CustomSTM32WLxWrapper : public RadioLibWrapper {
public:
  CustomSTM32WLxWrapper(CustomSTM32WLx& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    ((CustomSTM32WLx *)_radio)->setFrequency(freq);
    ((CustomSTM32WLx *)_radio)->setSpreadingFactor(sf);
    ((CustomSTM32WLx *)_radio)->setBandwidth(bw);
    ((CustomSTM32WLx *)_radio)->setCodingRate(cr);
    updatePreamble(sf);
  }

  bool isReceivingPacket() override { 
    return ((CustomSTM32WLx *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSTM32WLx *)_radio)->getRSSI(false);
  }
  float getLastRSSI() const override { return ((CustomSTM32WLx *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomSTM32WLx *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSTM32WLx *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  uint8_t getSpreadingFactor() const override { return ((CustomSTM32WLx *)_radio)->spreadingFactor; }

  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio); }
};
