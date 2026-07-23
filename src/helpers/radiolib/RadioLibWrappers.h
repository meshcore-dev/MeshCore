#pragma once

#include <Mesh.h>
#include <RadioLib.h>

struct PacketMillis {
  uint32_t preambleMillis;  // preamble-detect -> header-valid deadline
  uint32_t payloadMillis;   // header-valid   -> rx-done deadline
};

class RadioLibWrapper : public mesh::Radio {
protected:
  PhysicalLayer* _radio;
  mesh::MainBoard* _board;
  static constexpr uint16_t NUM_NOISE_FLOOR_SAMPLES = 64;
  static constexpr uint16_t DEFAULT_NOISE_FLOOR_SAMPLE_INTERVAL_MS = 50;
  static constexpr uint32_t DEFAULT_NOISE_FLOOR_MAX_CALIB_WINDOW_MS = 60000;
  static constexpr int16_t DEFAULT_NOISE_FLOOR_LOW_BOUND = -125;
  static constexpr int16_t DEFAULT_NOISE_FLOOR_HIGH_BOUND = -80;
  uint32_t n_recv, n_sent, n_recv_errors;
  int16_t _noise_floor, _threshold;
  bool _cad_enabled;
  int16_t _noise_floor_low_bound, _noise_floor_high_bound;
  float _last_packet_rssi, _last_packet_snr;
  uint16_t _num_floor_samples;
  int16_t _floor_samples[NUM_NOISE_FLOOR_SAMPLES];
  int16_t _floor_sample_min, _floor_sample_median, _floor_sample_max;
  uint16_t _floor_rejected_low_bound;
  uint16_t _floor_rejected_high_bound;
  uint16_t _noise_floor_sample_interval_ms;
  uint32_t _noise_floor_max_calib_window_ms;
  unsigned long _noise_floor_calibration_scheduled_at;
  unsigned long _noise_floor_batch_started_at;
  unsigned long _last_noise_floor_sample_at;
  bool _noise_floor_batch_active;
  bool _has_last_noise_floor_sample;
  uint8_t _preamble_sf;

  void resetNoiseFloorSamples();
  void resetNoiseFloorBatch();
  void idle();
  void startRecv();
  bool hasNoiseFloor() const;
  virtual unsigned long getMillis() const;
  void updateLastPacketMetrics(float rssi, float snr) {
    _last_packet_rssi = rssi;
    _last_packet_snr = snr;
  }
  float packetScoreInt(float snr, int sf, int packet_len);
  virtual bool isReceivingPacket() =0;
  virtual void doResetAGC();

public:
  RadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board) :
      _radio(&radio), _board(&board), n_recv(0), n_sent(0), n_recv_errors(0),
      _noise_floor(0), _threshold(0),
      _noise_floor_low_bound(DEFAULT_NOISE_FLOOR_LOW_BOUND),
      _noise_floor_high_bound(DEFAULT_NOISE_FLOOR_HIGH_BOUND),
      _last_packet_rssi(0), _last_packet_snr(0),
      _num_floor_samples(0), _floor_sample_min(0), _floor_sample_median(0),
      _floor_sample_max(0), _floor_rejected_low_bound(0),
      _floor_rejected_high_bound(0),
      _noise_floor_sample_interval_ms(DEFAULT_NOISE_FLOOR_SAMPLE_INTERVAL_MS),
      _noise_floor_max_calib_window_ms(DEFAULT_NOISE_FLOOR_MAX_CALIB_WINDOW_MS),
      _noise_floor_calibration_scheduled_at(0),
      _noise_floor_batch_started_at(0), _last_noise_floor_sample_at(0),
      _noise_floor_batch_active(false), _has_last_noise_floor_sample(false),
      _preamble_sf(0) { }

  void begin() override;
  virtual void powerOff() { _radio->sleep(); }
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;
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
  PacketMillis calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint8_t preambleSymbols);
  virtual int16_t performChannelScan();

  int getNoiseFloor() const override;
  mesh::NoiseFloorStats getNoiseFloorStats() const override;
  void setNoiseFloorCalibration(uint16_t sample_interval_ms, uint32_t max_calib_window_ms) override;
  void setNoiseFloorClamps(int16_t low_bound, int16_t high_bound) override;
  void scheduleNoiseFloorCalibration(uint32_t settle_ms) override;
  void triggerNoiseFloorCalibrate(int threshold) override;
  void setCADEnabled(bool enable) override { _cad_enabled = enable; }
  void resetAGC() override;

  void loop() override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return n_recv_errors; }
  uint32_t getPacketsSent() const { return n_sent; }
  void resetStats() { n_recv = n_sent = n_recv_errors = 0; }

  virtual float getLastRSSI() const override { return _last_packet_rssi; }
  virtual float getLastSNR() const override { return _last_packet_snr; }

  float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, 10, packet_len); }  // assume sf=10

  virtual bool setRxBoostedGainMode(bool) { return false; }
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
