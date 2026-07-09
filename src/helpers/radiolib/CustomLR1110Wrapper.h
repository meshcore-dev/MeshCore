#pragma once

#include "CustomLR1110.h"
#include "RadioLibWrappers.h"
#include "LR11x0Reset.h"

class CustomLR1110Wrapper : public RadioLibWrapper {
public:
  CustomLR1110Wrapper(CustomLR1110& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    ((CustomLR1110 *)_radio)->setFrequency(freq);
    ((CustomLR1110 *)_radio)->setSpreadingFactor(sf);
    ((CustomLR1110 *)_radio)->setBandwidth(bw);
    ((CustomLR1110 *)_radio)->setCodingRate(cr);
    updatePreamble(sf);
  }

  void doResetAGC() override { lr11x0ResetAGC((LR11x0 *)_radio, ((CustomLR1110 *)_radio)->getFreqMHz()); }
  bool isReceivingPacket() override {
    return ((CustomLR1110 *)_radio)->isReceiving();
  }
  bool isChipBusy() override {
    return ((CustomLR1110 *)_radio)->isChipBusy();
  }
  float getCurrentRSSI() override {
    float rssi = -110;
    ((CustomLR1110 *)_radio)->getRssiInst(&rssi);
    return rssi;
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    _radio->setPreambleLength(preambleLengthForSF(getSpreadingFactor())); // overcomes weird issues with small and big pkts
  }

  bool supportsRxPowerSaving() const override { return true; }

protected:
  int startReceiveMode() override {
    if (_rx_ps_armed) {
      // stop the previous duty-cycle sequence with an explicit standby before
      // reconfiguring the radio
      stopReceiveDutyCycle();
    }
    if (!_rx_ps_enabled || _nf_calib_active) {
      // plain continuous RX: powersaving off, or a periodic noise-floor
      // calibration window is in progress
      return _radio->startReceive();
    }

    const RadioLibIrqFlags_t irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS;
    // route RX timeout (false preamble detect) and error IRQs to the IRQ pin
    // as well, so the wrapper re-arms RX instead of waiting forever
    const RadioLibIrqFlags_t irqMask =
        (1UL << RADIOLIB_IRQ_RX_DONE) |
        (1UL << RADIOLIB_IRQ_TIMEOUT) |
        (1UL << RADIOLIB_IRQ_CRC_ERR) |
        (1UL << RADIOLIB_IRQ_HEADER_ERR);

    int err = ((CustomLR1110 *)_radio)->startReceiveDutyCycle(_rx_ps_rx_us, _rx_ps_sleep_us, irqFlags, irqMask);
    if (err == RADIOLIB_ERR_NONE) {
      _rx_ps_armed = true;
      return err;
    }

    MESH_DEBUG_PRINTLN("CustomLR1110Wrapper: error: startReceiveDutyCycle(%d), falling back to continuous RX", err);
    return _radio->startReceive();
  }

public:
  float getLastRSSI() const override { return ((CustomLR1110 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomLR1110 *)_radio)->getSNR(); }

  uint8_t getSpreadingFactor() const override { return ((CustomLR1110 *)_radio)->getSpreadingFactor(); }
  
  void setRxBoostedGainMode(bool en) override {
    ((CustomLR1110 *)_radio)->setRxBoostedGainMode(en);
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomLR1110 *)_radio)->getRxBoostedGainMode();
  }
};
