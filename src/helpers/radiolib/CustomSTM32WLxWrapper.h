#pragma once

#include "CustomSTM32WLx.h"
#include "RadioLibWrappers.h"
#include <math.h>

class CustomSTM32WLxWrapper : public RadioLibWrapper {
public:
  CustomSTM32WLxWrapper(CustomSTM32WLx& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
protected:
  int tryReadRawWithMeta(uint8_t* bytes, int sz, int* out_len) override {
    size_t len = 0;
    int err = ((CustomSTM32WLx*)_radio)->tryReadData(bytes, (size_t)sz, &len);
    if (out_len) {
      *out_len = (err == RADIOLIB_ERR_NONE) ? (int)len : 0;
    }
    return err;
  }
public:
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
};
