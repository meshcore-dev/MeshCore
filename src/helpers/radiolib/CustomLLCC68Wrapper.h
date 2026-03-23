#pragma once

#include "CustomLLCC68.h"
#include "RadioLibWrappers.h"

class CustomLLCC68Wrapper : public RadioLibWrapper {
public:
  CustomLLCC68Wrapper(CustomLLCC68& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
protected:
  int tryReadRawWithMeta(uint8_t* bytes, int sz, int* out_len) override {
    size_t len = 0;
    int err = ((CustomLLCC68*)_radio)->tryReadData(bytes, (size_t)sz, &len);
    if (out_len) {
      *out_len = (err == RADIOLIB_ERR_NONE) ? (int)len : 0;
    }
    return err;
  }
public:
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
};
