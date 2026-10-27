#pragma once

#include "CustomLR2021.h"
#include "RadioLibWrappers.h"

#ifndef USE_LR2021
#define USE_LR2021
#endif

#ifndef LR2021_RX_BOOST_LEVEL
#define LR2021_RX_BOOST_LEVEL 7
#endif

class CustomLR2021Wrapper : public RadioLibWrapper {
public:
  CustomLR2021Wrapper(CustomLR2021& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  bool applyParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    if (((CustomLR2021 *)_radio)->setFrequency(freq) != RADIOLIB_ERR_NONE) return false;
    if (((CustomLR2021 *)_radio)->setSpreadingFactor(sf) != RADIOLIB_ERR_NONE) return false;
    if (((CustomLR2021 *)_radio)->setBandwidth(bw) != RADIOLIB_ERR_NONE) return false;
    if (((CustomLR2021 *)_radio)->setCodingRate(cr) != RADIOLIB_ERR_NONE) return false;
    if (!updatePreamble(sf)) return false;
    if (applySideDetectorConfig(bw) != RADIOLIB_ERR_NONE) return false;
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, currentPreambleLength());
    ((CustomLR2021 *)_radio)->setPreambleMillis(pm.preambleMillis);
    ((CustomLR2021 *)_radio)->setMaxPayloadMillis(pm.payloadMillis);
    return true;
  }

  bool configSideDetectors(const uint8_t* sideDetSFs, uint8_t num, float bw) override {
    if (_profiles.enabled() || num > 3 || (num && sideDetSFs == nullptr)) return false;
    LR2021LoRaSideDetector_t tmp[3];
    uint8_t sf = getSpreadingFactor();

    if (sf >= 10 && num > 1) { return false; }  // only 1 side detector allowed when primary SF >= 10
    for (int i = 0; i < num; i++) {
      if (sideDetSFs[i] > 12 || sideDetSFs[i] < 5) { return false; }  // must be valid SF
      if (sideDetSFs[i] <= sf) { return false; }  // must be < primary SF
      if (sideDetSFs[i] > sf + 4) { return false; }  // span must not be > 4

      tmp[i].sf = sideDetSFs[i];
      float tSym = calcTsym(tmp[i].sf, bw);
      if (tSym >= 16.0f) {
        tmp[i].ldro = true;
      } else {
        tmp[i].ldro = false; 
      }
      tmp[i].invertIQ = false;
      tmp[i].syncWord = 0x12;
    }
    int16_t status = ((CustomLR2021 *)_radio)->setSideDetector(tmp, num);
    RadioLibWrapper::idle(); // trigger startReceive()
    MESH_DEBUG_PRINTLN("setSideDetector() returned %d", status);

    if (status == RADIOLIB_ERR_NONE) {
      for (int i = 0; i < num; i++) { _sideDet[i] = tmp[i]; }
      _numSideDet = num;
    } else {
      return false;
    }
    
    return true;
  }

  int16_t applySideDetectorConfig(float bw) {
    // Side detector SFs belong to the saved single-profile configuration.
    // Suspend them while scanning profiles so a different SF/BW tuple cannot
    // inherit incompatible detector settings. Restore them when scanning ends.
    if (_profiles.enabled()) return ((CustomLR2021 *)_radio)->setSideDetector(nullptr, 0);
    LR2021LoRaSideDetector_t detectors[3];
    for (size_t i = 0; i < _numSideDet; ++i) {
      detectors[i] = _sideDet[i];
      detectors[i].ldro = calcTsym(detectors[i].sf, bw) >= 16.0f;
    }
    return ((CustomLR2021 *)_radio)->setSideDetector(_numSideDet ? detectors : nullptr, _numSideDet);
  }

  float calcTsym(uint8_t sf, float bw) {
    float tSym = (float)(uint32_t(1) << sf) / (float)bw;
    return tSym;
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
    _radio->setPreambleLength(currentPreambleLength()); // overcomes weird issues with small and big pkts
  }


  uint8_t getSpreadingFactor() const override { return ((CustomLR2021 *)_radio)->getSpreadingFactor(); }
  
  bool setRxBoostedGainMode(bool en) override {
    ((CustomLR2021 *)_radio)->standby(); // LR2021 must be in standby to accept setRxBoostedGainMode
    int16_t status = ((CustomLR2021 *)_radio)->setRxBoostedGainMode(en ? LR2021_RX_BOOST_LEVEL: 0);
    RadioLibWrapper::idle(); // trigger startReceive()
    return status == RADIOLIB_ERR_NONE;
  }

  bool getRxBoostedGainMode() const override {
    return ((CustomLR2021 *)_radio)->getRxBoostedGainMode();
  }

  protected:
    LR2021LoRaSideDetector_t _sideDet[3];
    size_t _numSideDet = 0;

};
