#pragma once

#include <Mesh.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

class ESPNOWRadio : public mesh::Radio {
protected:
  uint32_t n_recv, n_sent;
  struct PendingFrame {
    uint16_t len;
    uint8_t buf[256];
  };

  static const uint8_t RX_QUEUE_SIZE = 4;
  PendingFrame _rx_queue[RX_QUEUE_SIZE];
  uint8_t _rx_read_idx;
  uint8_t _rx_write_idx;
  uint8_t _rx_count;
  uint32_t _rx_drop_count;
  portMUX_TYPE _rx_lock;
  bool popPendingFrame(PendingFrame& frame);

public:
  ESPNOWRadio() : _rx_read_idx(0), _rx_write_idx(0), _rx_count(0), _rx_drop_count(0), _rx_lock(portMUX_INITIALIZER_UNLOCKED) { n_recv = n_sent = 0; }

  void init();
  bool enqueueRxFrame(const uint8_t* data, int len);
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return 0; }
  uint32_t getPacketsSent() const { return n_sent; }
  void resetStats() { n_recv = n_sent = 0; }
  void setRxBoostedGainMode(bool) { }
  void powerOff() { }

  virtual float getLastRSSI() const override;
  virtual float getLastSNR() const override;

  float packetScore(float snr, int packet_len) override { return 0; }
  uint32_t intID();
  void setTxPower(uint8_t dbm);
};

#if ESPNOW_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define ESPNOW_DEBUG_PRINT(F, ...) Serial.printf("ESP-Now: " F, ##__VA_ARGS__)
  #define ESPNOW_DEBUG_PRINTLN(F, ...) Serial.printf("ESP-Now: " F "\n", ##__VA_ARGS__)
#else
  #define ESPNOW_DEBUG_PRINT(...) {}
  #define ESPNOW_DEBUG_PRINTLN(...) {}
#endif
