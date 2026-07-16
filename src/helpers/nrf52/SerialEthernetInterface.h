#pragma once

#include "helpers/BaseSerialInterface.h"
#include <SPI.h>
#include <RAK13800_W5100S.h>

#ifndef ETHERNET_TCP_PORT
  #define ETHERNET_TCP_PORT 5000
#endif

#ifndef MAX_ETH_CLIENTS
  #define MAX_ETH_CLIENTS 3
#endif

#ifndef ETH_FRAME_QUEUE_SIZE
  #define ETH_FRAME_QUEUE_SIZE 16
#endif

class SerialEthernetInterface : public BaseSerialInterface {
public:
  struct FrameHeader {
    uint8_t state;
    uint16_t frame_len;
    uint16_t rx_len;
    uint8_t rx_buf[MAX_FRAME_SIZE];
  };

private:
  struct Frame {
    uint16_t len;
    int8_t target;
    bool broadcast;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  bool deviceConnected;
  bool _isEnabled;
  unsigned long _last_write;
  int _last_rx;
  int _rr;

  EthernetServer server;
  EthernetClient clients[MAX_ETH_CLIENTS];
  FrameHeader rx_header[MAX_ETH_CLIENTS];

  int send_queue_len;
  Frame send_queue[ETH_FRAME_QUEUE_SIZE];

  void clearClientState(int idx);
  void clearBuffers() {
    send_queue_len = 0;
    for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
      clearClientState(i);
    }
  }

protected:

public:
    SerialEthernetInterface() : server(ETHERNET_TCP_PORT) {
        deviceConnected = false;
        _isEnabled = false;
        _last_write = 0;
        _last_rx = -1;
        _rr = 0;
        send_queue_len = 0;
        for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
          clearClientState(i);
        }
    }
    bool begin();

    // BaseSerialInterface methods
    void enable() override;
    void disable() override;
    bool isEnabled() const override { return _isEnabled; }

    bool isConnected() const override;
    bool isWriteBusy() const override;

    size_t writeFrame(const uint8_t src[], size_t len) override;
    size_t checkRecvFrame(uint8_t dest[]) override;

    void loop();
};


#if ETHERNET_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define ETHERNET_DEBUG_PRINT(F, ...) Serial.printf("ETH: " F, ##__VA_ARGS__)
  #define ETHERNET_DEBUG_PRINTLN(F, ...) Serial.printf("ETH: " F "\n", ##__VA_ARGS__)
  #define ETHERNET_DEBUG_PRINT_IP(name, ip) Serial.printf(name ": %u.%u.%u.%u" "\n", ip[0], ip[1], ip[2], ip[3])
#else
  #define ETHERNET_DEBUG_PRINT(...) {}
  #define ETHERNET_DEBUG_PRINTLN(...) {}
  #define ETHERNET_DEBUG_PRINT_IP(...) {}
#endif
