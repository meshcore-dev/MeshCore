#pragma once

#include <Mesh.h>

class ESPNOWRadio : public mesh::Radio {
protected:
  uint32_t n_recv, n_sent;

public:
  ESPNOWRadio() { n_recv = n_sent = 0; }

  void init();
  int recvRaw(uint8_t *bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  bool startSendRaw(const uint8_t *bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsSent() const { return n_sent; }
  void resetStats() { n_recv = n_sent = 0; }

  virtual float getLastRSSI() const override;
  virtual float getLastSNR() const override;

  float packetScore(float snr, int packet_len) override { return 0; }
  uint32_t intID();
  void setTxPower(uint8_t dbm);
};

#if ESPNOW_DEBUG_LOGGING && ARDUINO
#include <Arduino.h>
#define ESPNOW_DEBUG_PRINT(F, ...)   Serial.printf("ESP-Now: " F, ##__VA_ARGS__)
#define ESPNOW_DEBUG_PRINTLN(F, ...) Serial.printf("ESP-Now: " F "\n", ##__VA_ARGS__)
#else
#define ESPNOW_DEBUG_PRINT(...) \
  {}
#define ESPNOW_DEBUG_PRINTLN(...) \
  {}
#endif
