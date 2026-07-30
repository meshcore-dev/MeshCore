#pragma once

#include "CustomLR2021.h"
#include "RadioLibWrappers.h"

// MeshCore wrapper for the LR2021. Implements the RadioLibWrapper hooks the mesh
// engine needs, using the LR2021's RadioLib API (getRssiInst / getRSSI / getSNR /
// boosted-gain). doResetAGC() is left to the base-class default for now; add an
// LR2021-specific reset only if RX sensitivity degrades over long runs.

class CustomLR2021Wrapper : public RadioLibWrapper {
public:
  CustomLR2021Wrapper(CustomLR2021& radio, mesh::MainBoard& board)
    : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    ((CustomLR2021 *)_radio)->setFrequency(freq);
    ((CustomLR2021 *)_radio)->setSpreadingFactor(sf);
    ((CustomLR2021 *)_radio)->setBandwidth(bw);
    ((CustomLR2021 *)_radio)->setCodingRate(cr);
    updatePreamble(sf);
  }

  bool isReceivingPacket() override {
    return ((CustomLR2021 *)_radio)->isReceiving();
  }

  void onBeforeStartRecv() override {
    // re-arming (setRxPath) while still in continuous RX -> CMD_PERR (-706)
    // and a wedged receiver; drop to standby before every startReceive()
    _radio->standby();
  }

  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR2021 *)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    _radio->setPreambleLength(16);  // overcomes weird issues with small and big pkts
  }

  float getLastRSSI() const override { return ((CustomLR2021 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomLR2021 *)_radio)->getSNR(); }

  void setRxBoostedGainMode(bool en) override {
    ((CustomLR2021 *)_radio)->setRxBoostedGainMode(en ? LR2021_RX_BOOST_LEVEL : 0);
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomLR2021 *)_radio)->getRxBoostLevel() != 0;
  }
};
