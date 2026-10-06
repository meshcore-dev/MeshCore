#pragma once
#include <Mesh.h>

// SX1262 driver for the ASR6502 on top of the Heltec radio API (::Radio.*). The Heltec header is only included in
// CubeCellRadio.cpp. State machine: RX (continuous) <-> TX; every RxDone/RxError/TxDone/TxTimeout returns to RX.
//
// Threading: the Heltec driver runs RxDone/RxError/TxDone (DIO1 interrupt) and TxTimeout (timer interrupt) INSIDE
// interrupts. Fields written by the callbacks are volatile and are read/copied inside a critical section; every
// main-context ::Radio.* call runs with the DIO1 interrupt detached (RadioLock in the .cpp).
class CubeCellRadio : public mesh::Radio {
  enum { ST_IDLE = 0, ST_RX = 1, ST_TX = 2 };
  volatile uint8_t _state;
  volatile bool _rx_ready, _tx_done, _tx_timed_out;
  uint8_t _rx_buf[255];
  volatile uint16_t _rx_len;
  volatile int16_t _last_rssi;
  volatile int8_t _last_snr;
  float _freq, _bw;
  uint8_t _sf, _cr;
  int8_t _pwr;
  bool _boost, _inited;
  int16_t _noise_floor;
  uint32_t _last_nf_sample;
  uint32_t _n_recv, _n_sent;
  volatile uint32_t _n_err, _n_dropped, _n_txto;
  uint32_t _n_blocked;

  void applyRx();
  void applyTx(uint32_t timeout_ms);
  void startRx();

public:
  CubeCellRadio();
  void init();                               // ::Radio.Init (idempotent)
  uint32_t random32();                       // ::Radio.Random(); only before begin()
  uint32_t getRngSeed();                     // fold of 8 Radio.Random() values (wideband RSSI noise); before begin()
  void begin() override;
  void loop() override;
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  float packetScore(float snr, int packet_len) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override { return _state == ST_RX; }
  int getNoiseFloor() const override { return _noise_floor; }
  float getLastRSSI() const override { return _last_rssi; }
  float getLastSNR() const override { return _last_snr; }

  void setParams(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr);
  void setTxPower(int8_t dbm);
  bool setRxBoostedGainMode(bool en) { _boost = en; return false; }   // not supported: Radio.Rx() is used, not RxBoosted
  bool getRxBoostedGainMode() const { return _boost; }
  uint32_t getPacketsRecv() const { return _n_recv; }
  uint32_t getPacketsSent() const { return _n_sent; }
  uint32_t getPacketsRecvErrors() const { return _n_err + _n_dropped; }   // CRC/header errors + packets lost to a busy buffer
  uint32_t getPacketsBlocked() const { return _n_blocked; }

  // called from the static Heltec callbacks (interrupt context)
  void handleRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr);
  void handleRxError();
  void handleTxDone(bool timeout);
};
