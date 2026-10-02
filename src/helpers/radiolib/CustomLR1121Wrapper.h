#pragma once

#include "CustomLR1121.h"
#include "RadioLibWrappers.h"
#include "LR11x0Reset.h"

class CustomLR1121Wrapper : public RadioLibWrapper {
public:
  CustomLR1121Wrapper(CustomLR1121& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    // LR1121 supports regular bandwidths 62.5,125,250,500 KHz and SX128x compatible 203.125,406.25,812.5 KHz
    // Legacy SX128x 2.4GHz bandwidths available only if frequency > 1000 MHz, 203.125,406.25,812.5 KHz
    bool legacy_hf_bw_mode = (freq>1000) && ((fabsf(bw - 203.125f) <= 0.001f) || (fabsf(bw - 406.25f) <= 0.001f) || (fabsf(bw - 812.5f) <= 0.001f));

    ((CustomLR1121 *)_radio)->setFrequency(freq);
    ((CustomLR1121 *)_radio)->setSpreadingFactor(sf);
    ((CustomLR1121 *)_radio)->setBandwidth(bw,legacy_hf_bw_mode);
    ((CustomLR1121 *)_radio)->setCodingRate(cr);
    updatePreamble(sf);
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, preambleLengthForSF(sf));
    ((CustomLR1121 *)_radio)->setPreambleMillis(pm.preambleMillis);
    ((CustomLR1121 *)_radio)->setMaxPayloadMillis(pm.payloadMillis);
  }

  void doResetAGC() override { lr11x0ResetAGC((LR11x0 *)_radio, ((CustomLR1121 *)_radio)->getFreqMHz(), getRxBoostedGainMode()); }
  bool isReceivingPacket() override {
    return ((CustomLR1121 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR1121 *)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    _radio->setPreambleLength(preambleLengthForSF(getSpreadingFactor())); // overcomes weird issues with small and big pkts
  }

  uint32_t getEstAirtimeFor(int len_bytes) override {
    auto airtime = RadioLibWrapper::getEstAirtimeFor(len_bytes);
    return airtime < 200 ? 200 : airtime;   // at least 200 millis
  }

  float getLastRSSI() const override { return ((CustomLR1121 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomLR1121 *)_radio)->getSNR(); }

  uint8_t getSpreadingFactor() const override { return ((CustomLR1121 *)_radio)->getSpreadingFactor(); }
  
  bool setRxBoostedGainMode(bool en) override {
    return ((CustomLR1121 *)_radio)->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomLR1121 *)_radio)->getRxBoostedGainMode();
  }
};
