#pragma once

#include <Mesh.h>
#include <RadioLib.h>

// Fallback RX powersaving timings, only used until setRxPowerSaving() is called
// (begin() always applies the persisted values). The authoritative defaults live
// in CommonCLI.h as RX_POWERSAVING_DEFAULT_RX_US / _SLEEP_US and are delivered
// via NodePrefs; keep these mirrored.
#define RX_PS_FALLBACK_RX_US    65625UL
#define RX_PS_FALLBACK_SLEEP_US 60000UL

class RadioLibWrapper : public mesh::Radio {
protected:
  PhysicalLayer* _radio;
  mesh::MainBoard* _board;
  uint32_t n_recv, n_sent, n_recv_errors;
  int16_t _noise_floor, _threshold;
  float _last_rssi, _last_snr;
  bool _cad_enabled;
  uint16_t _num_floor_samples;
  int32_t _floor_sample_sum;
  uint8_t _preamble_sf;
  bool _rx_ps_enabled;
  bool _rx_ps_armed;      // radio is currently in RX duty-cycle mode
  bool _rx_hold_continuous;
  uint32_t _rx_ps_rx_us;
  uint32_t _rx_ps_sleep_us;

  // RX duty-cycle watchdog: a healthy duty cycle shows a square wave on the
  // BUSY pin (high in the sleep window / TCXO warmup, low while listening).
  // If the wave stops, the chip fell out of the cycle without an IRQ.
  // Passive sampling only works while the main loop spins; on MCUs that light
  // sleep between wakeups the watchdog instead opens an "active observation"
  // window (isWatchdogObserving() keeps the MCU awake) spanning one full radio
  // cycle - a healthy chip must toggle BUSY within it.
  bool _wd_last_busy;
  uint8_t _wd_stage;                  // 0 = healthy, 1 = soft re-arm done, 2 = hard reset done
  uint8_t _wd_strikes;                // consecutive failed observation windows
  uint8_t _startrx_fails;             // consecutive startReceiveMode() failures
  unsigned long _wd_last_transition;  // millis of last BUSY level change (proof of life)
  unsigned long _wd_stuck_thresh;     // ms without proof of life before observing
  unsigned long _wd_observe_until;    // 0 = not observing, else millis deadline
  uint32_t _wd_observe_ms;            // observation window: one full cycle + margin
  uint32_t n_wd_soft, n_wd_hard;

  // last applied radio settings, reapplied after a hard radio reset
  float _cur_freq, _cur_bw;
  uint8_t _cur_sf, _cur_cr;
  int8_t _cur_dbm;
  bool _params_valid, _dbm_valid;

  // Initial and periodic noise-floor calibration (only while RX duty-cycle
  // powersaving is armed): a duty-cycled receiver can't be sampled reliably,
  // so at least once a minute the wrapper drops to plain continuous RX,
  // collects a fresh sample batch, publishes it and re-arms the duty cycle.
  bool _nf_calib_active;
  unsigned long _nf_last_calib;       // millis of last completed/attempted window
  unsigned long _nf_calib_deadline;   // abort window if the batch can't complete
  unsigned long _nf_sample_from;      // no samples before this (RX entry settle)

  void idle();
  void startRecv();
  void rxPsWatchdogCheck();
  void noiseFloorCalibCheck();
  void endNoiseFloorCalib(unsigned long now);
  void cacheParams(float freq, float bw, uint8_t sf, uint8_t cr) {
    _cur_freq = freq; _cur_bw = bw; _cur_sf = sf; _cur_cr = cr; _params_valid = true;
  }
  virtual int startReceiveMode();
  virtual void stopReceiveDutyCycle();
  virtual bool isPacketReady();
  // true while the chip cannot service SPI (RX duty-cycle sleep window, or
  // briefly while processing a command); radios expose this via the BUSY pin
  virtual bool isChipBusy() { return false; }
  // full radio recovery: hardware reset (NRST) + re-init to boot defaults;
  // returns false if unsupported. Caller reapplies cached runtime params.
  virtual bool radioDeepInit() { return false; }
  float packetScoreInt(float snr, int sf, int packet_len);
  virtual bool isReceivingPacket() =0;
  virtual void doResetAGC();

public:
  RadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board)
      : _radio(&radio), _board(&board), _preamble_sf(0), _rx_ps_enabled(false), _rx_ps_armed(false),
        _rx_hold_continuous(false),
        _rx_ps_rx_us(RX_PS_FALLBACK_RX_US), _rx_ps_sleep_us(RX_PS_FALLBACK_SLEEP_US),
        _wd_last_busy(false), _wd_stage(0), _wd_strikes(0), _startrx_fails(0), _wd_last_transition(0),
        _wd_stuck_thresh(0), _wd_observe_until(0), _wd_observe_ms(0),
        _params_valid(false), _dbm_valid(false),
        _nf_calib_active(false), _nf_last_calib(0), _nf_calib_deadline(0), _nf_sample_from(0)
        { n_recv = n_sent = n_recv_errors = n_wd_soft = n_wd_hard = 0; _last_rssi = _last_snr = 0; }

  void begin() override;
  virtual void powerOff() { _radio->sleep(); }
  int recvRaw(uint8_t* bytes, int sz) override;
  void onReceiveProcessed() override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;
  bool setRxPowerSaving(bool enabled, uint32_t rx_us, uint32_t sleep_us) override;
  bool isChannelActive();

  bool isReceiving() override {
    if (isReceivingPacket()) return true;

    return isChannelActive();
  }

  virtual void setParams(float freq, float bw, uint8_t sf, uint8_t cr) = 0;
  uint32_t getRngSeed();
  void setTxPower(int8_t dbm);

  virtual float getCurrentRSSI() =0;
  virtual uint8_t getSpreadingFactor() const { return LORA_SF; }
  static uint16_t preambleLengthForSF(uint8_t sf) { return sf <= 8 ? 32 : 16; }
  void updatePreamble(uint8_t sf) { _preamble_sf = sf; _radio->setPreambleLength(preambleLengthForSF(sf)); }
  virtual int16_t performChannelScan();

  int getNoiseFloor() const override { return _noise_floor; }
  void triggerNoiseFloorCalibrate(int threshold) override;
  void setCADEnabled(bool enable) override { _cad_enabled = enable; }
  void resetAGC() override;

  void loop() override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return n_recv_errors; }
  uint32_t getPacketsSent() const { return n_sent; }
  uint32_t getRxPsWatchdogSoftCount() const { return n_wd_soft; }
  uint32_t getRxPsWatchdogHardCount() const { return n_wd_hard; }
  // true while the watchdog is actively watching for BUSY transitions; used by
  // the app's hasPendingWork() to keep the MCU out of light sleep for the window
  bool isWatchdogObserving() const { return _wd_observe_until != 0; }
  // true while a periodic noise-floor calibration window is in progress; the
  // app's hasPendingWork() must keep the MCU awake so the sample batch and the
  // return to duty-cycle complete promptly
  bool isCalibratingNoiseFloor() const { return _nf_calib_active; }
  void resetStats() { n_recv = n_sent = n_recv_errors = 0; }

  float getLastRSSI() const override final;
  float getLastSNR() const override final;

  float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, 10, packet_len); }  // assume sf=10

  virtual void setRxBoostedGainMode(bool) { }
  virtual bool getRxBoostedGainMode() const { return false; }
};

/**
 * \brief  an RNG impl using the noise from the LoRa radio as entropy.
 *         NOTE: this is VERY SLOW!  Use only for things like creating new LocalIdentity
*/
class RadioNoiseListener : public mesh::RNG {
  PhysicalLayer* _radio;
public:
  RadioNoiseListener(PhysicalLayer& radio): _radio(&radio) { }

  void random(uint8_t* dest, size_t sz) override {
    for (int i = 0; i < sz; i++) {
      dest[i] = _radio->randomByte() ^ (::random(0, 256) & 0xFF);
    }
  }
};
