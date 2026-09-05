#pragma once

// A no-hardware stand-in for the LoRa radio, so observer firmware can boot and
// run in an emulator (e.g. Wokwi) that models the ESP32-S3 + WiFi + display but
// has no SX1262. It's a drop-in for the concrete `radio_driver` used by the
// examples: it implements the mesh::Radio interface plus the RadioLibWrapper
// methods MyMesh/main call directly (setParams, setTxPower, getRngSeed, packet
// counters, …). Transmits "succeed" instantly with no RF; nothing is ever
// received. WiFi/MQTT/CLI/display all run normally on top of it.
//
// Compiled only into *_sim builds (guarded by SIM_BUILD in the target). Never
// pulled into real firmware.

#include <Mesh.h>
#include <stdint.h>
#include <stddef.h>
#if defined(ESP_PLATFORM)
  #include <esp_system.h>   // esp_random()
#endif

static inline uint32_t _simRandom() {
#if defined(ESP_PLATFORM)
  return esp_random();
#else
  return (uint32_t)millis() * 2654435761u;
#endif
}

// RNG for creating a LocalIdentity without radio noise (used by radio_new_identity()).
class SimRNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override {
    for (size_t i = 0; i < sz; i++) dest[i] = (uint8_t)_simRandom();
  }
};

class SimRadio : public mesh::Radio {
  uint32_t n_recv, n_sent, n_recv_errors;
  unsigned long _send_started;
public:
  explicit SimRadio(mesh::MainBoard& /*board*/) : n_recv(0), n_sent(0),
      n_recv_errors(0), _send_started(0) {}

  // --- mesh::Radio pure virtuals ---
  void begin() override {}
  int recvRaw(uint8_t* /*bytes*/, int /*sz*/) override { return 0; }  // never receives
  uint32_t getEstAirtimeFor(int len_bytes) override {
    return (uint32_t)(len_bytes < 0 ? 0 : len_bytes) * 10 + 10;       // rough, non-zero
  }
  float packetScore(float /*snr*/, int /*packet_len*/) override { return 0.0f; }
  bool startSendRaw(const uint8_t* /*bytes*/, int /*len*/) override {
    _send_started = millis();
    n_sent++;
    return true;                                                       // "sent" instantly
  }
  bool isSendComplete() override { return true; }
  void onSendFinished() override { _send_started = 0; }
  bool isInRecvMode() const override { return true; }

  // --- mesh::Radio overrides with useful sim values ---
  int getNoiseFloor() const override { return -110; }
  uint32_t getPacketsRecvErrors() const override { return n_recv_errors; }
  float getLastRSSI() const override { return -80.0f; }
  float getLastSNR() const override { return 9.0f; }

  // --- concrete RadioLibWrapper surface called directly on radio_driver ---
  void setParams(float /*freq*/, float /*bw*/, uint8_t /*sf*/, uint8_t /*cr*/) {}
  void setTxPower(int8_t /*dbm*/) {}
  uint32_t getRngSeed() { return _simRandom(); }
  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsSent() const { return n_sent; }
  void resetStats() { n_recv = n_sent = n_recv_errors = 0; }
  void setRxBoostedGainMode(bool) {}
  bool getRxBoostedGainMode() const { return false; }
};
