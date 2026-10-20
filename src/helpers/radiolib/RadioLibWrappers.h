#pragma once

#include <Mesh.h>
#include <RadioLib.h>
#include <RadioProfiles.h>

#ifdef USE_CC310_HW_CRYPTO
#include <Adafruit_nRFCrypto.h>
#endif
struct PacketMillis {
  uint32_t preambleMillis;  // preamble-detect -> header-valid deadline
  uint32_t payloadMillis;   // header-valid   -> rx-done deadline
};

class RadioLibWrapper : public mesh::Radio {
protected:
  PhysicalLayer* _radio;
  mesh::MainBoard* _board;
  uint32_t n_recv, n_sent, n_recv_errors;
  int16_t _noise_floor, _threshold;
  bool _cad_enabled;
  uint16_t _num_floor_samples;
  int32_t _floor_sample_sum;
  uint8_t _preamble_sf;

  void idle();
  bool startRecv();
  virtual bool applyParams(float freq, float bw, uint8_t sf, uint8_t cr) = 0;
  virtual bool isChipBusy() { return false; }
  virtual uint32_t minimumAirtimeMillis() const { return 0; }
  virtual void beginProfileRetune(bool) {}
  virtual void endProfileRetune(bool) {}
  virtual void setProfileStandbyWarm(bool) {}
  uint8_t beginReconfigure();
  mesh::RadioParamApplyResult tuneProfile(uint8_t profile);
  void serviceProfiles();
  void saveNoiseFloor(uint8_t profile);
  void loadNoiseFloor(uint8_t profile);

  mesh::RadioProfiles _profiles;
  mesh::RadioProfileParams _physical_params;
  uint8_t _state = 0, _active_profile = 0, _last_rx_profile = 0;
  uint32_t _profile_generation = 1, _last_rx_generation = 1;
  uint16_t _physical_preamble = 0;
  bool _params_valid = false, _profile_refresh_required = false, _physical_dual = false;
  uint32_t _profile_visit_stamp = 0;
  uint32_t _profile_scan_generation[2] = {};
  float _last_rssi = 0, _last_snr = 0;
  int16_t _profile_noise_floor[2] = {};
  uint16_t _profile_floor_samples[2] = {};
  int32_t _profile_floor_sum[2] = {};

  float packetScoreInt(float snr, int sf, int packet_len);
  virtual bool isReceivingPacket() =0;
  virtual void doResetAGC();

public:
  RadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board) : _radio(&radio), _board(&board), _preamble_sf(0) { n_recv = n_sent = n_recv_errors = 0; }

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

  mesh::RadioProfiles* profiles() override { return &_profiles; }
  const mesh::RadioProfiles* profiles() const override { return &_profiles; }
  bool validateProfile(const mesh::RadioProfileParams& params) const override;
  mesh::RadioParamApplyResult trySetPrimaryParams(const mesh::RadioProfileParams& params,
      bool temporary = false, const uint32_t* timings = nullptr) override;
  mesh::RadioParamApplyResult trySetParams(float freq, float bw, uint8_t sf, uint8_t cr,
      const uint32_t* timings = nullptr);
  bool setParams(float freq, float bw, uint8_t sf, uint8_t cr) {
    return trySetParams(freq, bw, sf, cr) == mesh::RadioParamApplyResult::APPLIED;
  }
  mesh::RadioParamApplyResult prepareTransmitProfile(uint8_t profile) override;
  uint32_t getProfileAirtime(uint8_t profile, int len_bytes, uint8_t cr = 0) override;
  uint8_t receiveProfile() const override { return _last_rx_profile; }
  uint32_t receiveProfileGeneration() const override { return _last_rx_generation; }
  uint16_t profilePreamble(uint8_t profile) const override {
    return _profiles.preamble(profile, preambleLengthForSF(_profiles.params(profile).sf));
  }
  uint32_t getRngSeed();
  void setTxPower(int8_t dbm);

  virtual float getCurrentRSSI() =0;
  virtual uint8_t getSpreadingFactor() const { return LORA_SF; }
  static uint16_t preambleLengthForSF(uint8_t sf) { return sf <= 8 ? 32 : 16; }
  bool updatePreamble(uint8_t sf) {
    _preamble_sf = sf;
    return _radio->setPreambleLength(_physical_preamble ? _physical_preamble : preambleLengthForSF(sf)) == RADIOLIB_ERR_NONE;
  }
  uint16_t currentPreambleLength() const { return _physical_preamble; }
  PacketMillis calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint16_t preambleSymbols);
  virtual int16_t performChannelScan();

  int getNoiseFloor() const override { return _noise_floor; }
  void triggerNoiseFloorCalibrate(int threshold) override;
  void setCADEnabled(bool enable) override { _cad_enabled = enable; }
  void resetAGC() override;

  void loop() override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return n_recv_errors; }
  uint32_t getPacketsSent() const { return n_sent; }
  void resetStats() { n_recv = n_sent = n_recv_errors = 0; }

  virtual float getLastRSSI() const override;
  virtual float getLastSNR() const override;

  float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, 10, packet_len); }  // assume sf=10

  virtual bool setRxBoostedGainMode(bool) { return false; }
  virtual bool getRxBoostedGainMode() const { return false; }
  
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) { return false; }
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
#ifdef USE_CC310_HW_CRYPTO
    nRFCrypto.Random.generate(dest, (uint16_t)sz);
    for (int i = 0; i < sz; i++) {
      dest[i] ^= _radio->randomByte() ^ (::random(0, 256) & 0xFF); // combine with Radio's entropy
    }
#else
    for (int i = 0; i < sz; i++) {
      dest[i] = _radio->randomByte() ^ (::random(0, 256) & 0xFF);
    }
#endif
  }
};
