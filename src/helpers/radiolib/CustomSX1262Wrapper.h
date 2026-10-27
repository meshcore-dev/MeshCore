#pragma once

#include "CustomSX1262.h"
#include "RadioLibWrappers.h"
#include "SX126xReset.h"

#ifndef USE_SX1262
#define USE_SX1262
#endif

class CustomSX1262Wrapper : public RadioLibWrapper {
  bool _warm_held = false, _saved_warm = false;
protected:
  void beginProfileRetune(bool continuousRx) override { static_cast<CustomSX1262*>(_radio)->beginProfileSwitch(continuousRx); }
  void endProfileRetune(bool success) override { static_cast<CustomSX1262*>(_radio)->endProfileSwitch(success); }
  bool isChipBusy() override { return static_cast<CustomSX1262*>(_radio)->isChipBusy(); }
  void setProfileStandbyWarm(bool enabled) override {
    if (enabled == _warm_held) return;
    auto* radio = static_cast<CustomSX1262*>(_radio);
    if (enabled) { _saved_warm = radio->standbyXOSC; radio->standbyXOSC = true; }
    else radio->standbyXOSC = _saved_warm;
    _warm_held = enabled;
  }

public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  bool applyParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    if (((CustomSX1262 *)_radio)->profileSwitchFailed()) return false;
    if (((CustomSX1262 *)_radio)->setFrequency(freq) != RADIOLIB_ERR_NONE) return false;
    if (((CustomSX1262 *)_radio)->setLoRaModulationParams(bw, sf, cr) != RADIOLIB_ERR_NONE) return false;
    if (!updatePreamble(sf)) return false;
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, currentPreambleLength());
    ((CustomSX1262 *)_radio)->setPreambleMillis(pm.preambleMillis);
    ((CustomSX1262 *)_radio)->setMaxPayloadMillis(pm.payloadMillis);
    return true;
  }

  bool isReceivingPacket() override { 
    return ((CustomSX1262 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1262 *)_radio)->getRSSI(false);
  }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1262 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  uint8_t getSpreadingFactor() const override { return ((CustomSX1262 *)_radio)->spreadingFactor; }
  virtual void powerOff() override {
    ((CustomSX1262 *)_radio)->sleep(false);
  }

  bool setRxBoostedGainMode(bool en) override {
    return ((CustomSX1262 *)_radio)->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomSX1262 *)_radio)->getRxBoostedGainMode();
  }

  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode()); }
};
