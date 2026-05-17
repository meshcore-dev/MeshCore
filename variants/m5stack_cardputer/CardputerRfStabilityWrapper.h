#pragma once

#include <helpers/radiolib/CustomSX1262Wrapper.h>

#ifndef CARDPUTER_RF_STABILITY_RX_REASSERT_MS
#define CARDPUTER_RF_STABILITY_RX_REASSERT_MS 30000UL
#endif

#ifndef CARDPUTER_RF_RX_WATCHDOG_SILENCE_MS
#define CARDPUTER_RF_RX_WATCHDOG_SILENCE_MS 180000UL
#endif

#ifndef CARDPUTER_RF_RX_WATCHDOG_INTERVAL_MS
#define CARDPUTER_RF_RX_WATCHDOG_INTERVAL_MS 60000UL
#endif

class CardputerRfStabilityWrapper : public CustomSX1262Wrapper {
  uint32_t _last_rx_boost_check_ms = 0;
  uint32_t _last_rx_packet_ms = 0;
  uint32_t _last_rx_watchdog_ms = 0;
  uint32_t _rx_watchdog_count = 0;

  void keepRxBoostedGain() {
#if defined(SX126X_RX_BOOSTED_GAIN) && SX126X_RX_BOOSTED_GAIN
    if (!getRxBoostedGainMode()) {
      setRxBoostedGainMode(true);
    }
#endif
  }

  void runRxWatchdogIfSilent(uint32_t now) {
    if (_last_rx_packet_ms != 0 && (uint32_t)(now - _last_rx_packet_ms) < CARDPUTER_RF_RX_WATCHDOG_SILENCE_MS) {
      return;
    }
    if ((uint32_t)(now - _last_rx_watchdog_ms) < CARDPUTER_RF_RX_WATCHDOG_INTERVAL_MS) {
      return;
    }
    if (isReceivingPacket()) {
      return;
    }

    _last_rx_watchdog_ms = now;
    keepRxBoostedGain();
    resetAGC();
    keepRxBoostedGain();
    _rx_watchdog_count++;
  }

public:
  CardputerRfStabilityWrapper(CustomSX1262& radio, mesh::MainBoard& board)
    : CustomSX1262Wrapper(radio, board) { }

  bool startSendRaw(const uint8_t* bytes, int len) override {
    // Keep the RX gain protection, but do not add custom TX delays here.
    // The MeshCore dispatcher already handles channel activity and retry timing.
    keepRxBoostedGain();
    return RadioLibWrapper::startSendRaw(bytes, len);
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    keepRxBoostedGain();
  }

  int recvRaw(uint8_t* bytes, int sz) override {
    int len = RadioLibWrapper::recvRaw(bytes, sz);
    if (len > 0) {
      _last_rx_packet_ms = millis();
      keepRxBoostedGain();
    }
    return len;
  }

  void loop() override {
    RadioLibWrapper::loop();
    uint32_t now = millis();
    if ((uint32_t)(now - _last_rx_boost_check_ms) >= CARDPUTER_RF_STABILITY_RX_REASSERT_MS) {
      _last_rx_boost_check_ms = now;
      keepRxBoostedGain();
    }
    runRxWatchdogIfSilent(now);
  }

  uint16_t getLastTxWaitMillis() const { return 0; }
  uint32_t getTxWindowCount() const { return 0; }
  uint16_t getLastRxGuardWaitMillis() const { return 0; }
  uint32_t getRxGuardCount() const { return 0; }
  uint32_t getRxWatchdogCount() const { return _rx_watchdog_count; }
  uint32_t getLastRxAgeMillis() const { return _last_rx_packet_ms == 0 ? 0xFFFFFFFFUL : millis() - _last_rx_packet_ms; }
};
