#pragma once

#include "CustomLR2021.h"
#include "RadioLibWrappers.h"

class CustomLR2021Wrapper : public RadioLibWrapper {
public:
  CustomLR2021Wrapper(CustomLR2021 &radio, mesh::MainBoard &board)
      : RadioLibWrapper(radio, board) {}

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

  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR2021 *)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    _radio->setPreambleLength(
        preambleLengthForSF(getSpreadingFactor()));
  }

  float getLastRSSI() const override {
    return ((CustomLR2021 *)_radio)->getRSSI();
  }

  float getLastSNR() const override {
    return ((CustomLR2021 *)_radio)->getSNR();
  }

  uint8_t getSpreadingFactor() const override {
    return ((CustomLR2021 *)_radio)->getSpreadingFactor();
  }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomLR2021 *)_radio)->getSpreadingFactor();
    return packetScoreInt(snr, sf, packet_len);
  }

  void powerOff() override {
    ((CustomLR2021 *)_radio)->sleep(false, 0);
  }

  void doResetAGC() override {
    CustomLR2021 *r = (CustomLR2021 *)_radio;
    float freq = r->getFreqMHz();
    r->sleep(true, 0);
    r->standby(RADIOLIB_LR2021_STANDBY_RC, true);
    r->calibrate(RADIOLIB_LR2021_CALIBRATE_ALL);
    r->setFrequency(freq);
    r->setRxBoostedGainMode(RADIOLIB_LR2021_RX_BOOST_LF);
  }

  void setRxBoostedGainMode(bool en) override {
    ((CustomLR2021 *)_radio)->setRxBoostedGainMode(
        en ? RADIOLIB_LR2021_RX_BOOST_LF : 0);
  }

  bool getRxBoostedGainMode() const override {
    return ((CustomLR2021 *)_radio)->getRxBoostedGainMode();
  }
};
