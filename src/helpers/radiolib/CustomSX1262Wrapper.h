#pragma once

#include "CustomSX1262.h"
#include "RadioLibWrappers.h"

class CustomSX1262Wrapper : public RadioLibWrapper {
public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { 
    return ((CustomSX1262 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1262 *)_radio)->getRSSI(false);
  }
  float getLastRSSI() const override { return ((CustomSX1262 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomSX1262 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1262 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  void onTXRXFault() override {
    auto radio = static_cast<CustomSX1262 *>(_radio);
    int16_t result = radio->reset();
    if (result != RADIOLIB_ERR_NONE) {
      MESH_DEBUG_PRINTLN("CustomSX1262Wrapper: reset failed (%d)", result);
      return;
    }
    if (!radio->std_init()) {
      MESH_DEBUG_PRINTLN("CustomSX1262Wrapper: re-init failed");
      return;
    }
    // Rebind ISR and restart receive after reset.
    RadioLibWrapper::begin();
    startRecv();
  }
  virtual void powerOff() override {
    ((CustomSX1262 *)_radio)->sleep(false);
  }
};
