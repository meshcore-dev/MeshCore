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
  bool _cad_enabled;
  uint16_t _num_floor_samples;
  int32_t _floor_sample_sum;
  uint8_t _preamble_sf;
  bool _rx_ps_enabled;
  bool _rx_ps_armed;      // radio is currently in RX duty-cycle mode
  uint32_t _rx_ps_rx_us;
  uint32_t _rx_ps_sleep_us;

  void idle();
  void startRecv();
  virtual int startReceiveMode();
  virtual void stopReceiveDutyCycle();
  virtual bool isPacketReady();
  // true while the chip cannot service SPI (RX duty-cycle sleep window, or
  // briefly while processing a command); radios expose this via the BUSY pin
  virtual bool isChipBusy() { return false; }
  float packetScoreInt(float snr, int sf, int packet_len);
  virtual bool isReceivingPacket() =0;
  virtual void doResetAGC();

public:
  RadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board)
      : _radio(&radio), _board(&board), _preamble_sf(0), _rx_ps_enabled(false), _rx_ps_armed(false),
        _rx_ps_rx_us(RX_PS_FALLBACK_RX_US), _rx_ps_sleep_us(RX_PS_FALLBACK_SLEEP_US) { n_recv = n_sent = n_recv_errors = 0; }

  void begin() override;
  virtual void powerOff() { _radio->sleep(); }
  int recvRaw(uint8_t* bytes, int sz) override;
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
  void resetStats() { n_recv = n_sent = n_recv_errors = 0; }

  virtual float getLastRSSI() const override;
  virtual float getLastSNR() const override;

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
