#pragma once

#include "CustomSX1262.h"
#include "RadioLibWrappers.h"
#include "SX126xReset.h"

#ifndef USE_SX1262
#define USE_SX1262
#endif

class CustomSX1262Wrapper : public RadioLibWrapper {
public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    cacheParams(freq, bw, sf, cr);
    ((CustomSX1262 *)_radio)->setFrequency(freq);
    ((CustomSX1262 *)_radio)->setSpreadingFactor(sf);
    ((CustomSX1262 *)_radio)->setBandwidth(bw);
    ((CustomSX1262 *)_radio)->setCodingRate(cr);
    updatePreamble(sf);
  }

  bool isReceivingPacket() override {
    return ((CustomSX1262 *)_radio)->isReceiving();
  }
  bool isChipBusy() override {
    return ((CustomSX1262 *)_radio)->isChipBusy();
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

  bool supportsRxPowerSaving() const override { return true; }

protected:
  int startReceiveMode() override {
    if (_rx_ps_armed) {
      // leaving duty-cycle mode (after RxDone or a reconfig): stop the
      // sequencer and the still-running RTC, or its pending event can
      // silently abort the RX we are about to start
      stopReceiveDutyCycle();
    }
    if (!_rx_ps_enabled || _nf_calib_active) {
      // plain continuous RX: powersaving off, or a periodic noise-floor
      // calibration window is in progress
      return _radio->startReceive();
    }

    const RadioLibIrqFlags_t irqFlags = RADIOLIB_IRQ_RX_DEFAULT_FLAGS;
    const RadioLibIrqFlags_t irqMask =
        (1UL << RADIOLIB_IRQ_RX_DONE) |
        (1UL << RADIOLIB_IRQ_TIMEOUT) |
        (1UL << RADIOLIB_IRQ_CRC_ERR) |
        (1UL << RADIOLIB_IRQ_HEADER_ERR);

    int err = ((CustomSX1262 *)_radio)->startReceiveDutyCycle(_rx_ps_rx_us, _rx_ps_sleep_us, irqFlags, irqMask);
    if (err == RADIOLIB_ERR_NONE) {
      _rx_ps_armed = true;
      return err;
    }

    MESH_DEBUG_PRINTLN("CustomSX1262Wrapper: error: startReceiveDutyCycle(%d), falling back to continuous RX", err);
    return _radio->startReceive();
  }

  void stopReceiveDutyCycle() override {
    _radio->standby();   // also wakes the chip if it is in the sleep window
    ((CustomSX1262 *)_radio)->stopRTC();
    _rx_ps_armed = false;
  }

  bool radioDeepInit() override {
    // std_init() re-runs RadioLib begin(), which starts with a hardware reset
    // via NRST - the only way out of a hard-locked chip (BUSY stuck high).
    return ((CustomSX1262 *)_radio)->std_init();
  }

public:
  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio); }

  void setRxBoostedGainMode(bool en) override {
    ((CustomSX1262 *)_radio)->setRxBoostedGainMode(en);
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomSX1262 *)_radio)->getRxBoostedGainMode();
  }
};
