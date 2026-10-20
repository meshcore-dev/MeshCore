#pragma once

#include "CustomLR1110.h"
#include "RadioLibWrappers.h"
#include "LR11x0Reset.h"

class CustomLR1110Wrapper : public RadioLibWrapper {
protected:
  void beginProfileRetune(bool continuousRx) override { static_cast<CustomLR1110*>(_radio)->beginProfileSwitch(continuousRx); }
  void endProfileRetune(bool success) override { static_cast<CustomLR1110*>(_radio)->endProfileSwitch(success); }
  void setProfileStandbyWarm(bool enabled) override { static_cast<CustomLR1110*>(_radio)->setProfileStandbyWarm(enabled); }
  bool isChipBusy() override { return static_cast<CustomLR1110*>(_radio)->isChipBusy(); }

public:
  uint32_t minimumAirtimeMillis() const override { return 200; }
  CustomLR1110Wrapper(CustomLR1110& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  bool applyParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    if (((CustomLR1110 *)_radio)->profileSwitchFailed()) return false;
    if (((CustomLR1110 *)_radio)->setFrequency(freq) != RADIOLIB_ERR_NONE) return false;
    if (((CustomLR1110 *)_radio)->setLoRaModulationParams(bw, sf, cr) != RADIOLIB_ERR_NONE) return false;
    if (!updatePreamble(sf)) return false;
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, currentPreambleLength());
    ((CustomLR1110 *)_radio)->setPreambleMillis(pm.preambleMillis);
    ((CustomLR1110 *)_radio)->setMaxPayloadMillis(pm.payloadMillis);
    return true;
  }

  bool isReceivingPacket() override {
    return ((CustomLR1110 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR1110 *)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  uint32_t getEstAirtimeFor(int len_bytes) override {
    auto airtime = RadioLibWrapper::getEstAirtimeFor(len_bytes);
    return airtime < 200 ? 200 : airtime;   // at least 200 millis
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    _radio->setPreambleLength(currentPreambleLength()); // overcomes weird issues with small and big pkts
  }


  uint8_t getSpreadingFactor() const override { return ((CustomLR1110 *)_radio)->getSpreadingFactor(); }
  
  bool setRxBoostedGainMode(bool en) override {
    return ((CustomLR1110 *)_radio)->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomLR1110 *)_radio)->getRxBoostedGainMode();
  }

  void doResetAGC() override { lr11x0ResetAGC((LR11x0 *)_radio, ((CustomLR1110 *)_radio)->getFreqMHz(), getRxBoostedGainMode()); }
};
