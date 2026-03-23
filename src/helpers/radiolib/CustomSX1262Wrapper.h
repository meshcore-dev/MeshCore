#pragma once

#include "CustomSX1262.h"
#include "RadioLibWrappers.h"

class CustomSX1262Wrapper : public RadioLibWrapper {
public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
protected:
  int tryReadRawWithMeta(uint8_t* bytes, int sz, int* out_len) override {
    size_t len = 0;
    int err = ((CustomSX1262*)_radio)->tryReadData(bytes, (size_t)sz, &len);
    if (out_len) {
      *out_len = (err == RADIOLIB_ERR_NONE) ? (int)len : 0;
    }
    return err;
  }
public:
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
  virtual void powerOff() override {
    ((CustomSX1262 *)_radio)->sleep(false);
  }
};
