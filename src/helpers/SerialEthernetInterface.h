#pragma once

#include "BaseSerialInterface.h"
#include <RAK13800_W5100S.h>

// Multi-client TCP companion interface over a W5100S Ethernet module (RAK13800).
// Lets several clients (e.g. Home Assistant AND the phone app) stay connected
// at once — the single-client model had them kicking each other off the one
// socket, causing an endless reconnect loop.
//
// Routing of outbound frames (the companion protocol isn't natively
// multi-client, so we route by frame code):
//   - PUSH frames  (code >= 0x80, e.g. LoRa-RX log, adverts) -> ALL clients
//   - command RESPONSES (code < 0x80)                        -> the client
//                                                               that issued
//                                                               the last command
//
// Ethernet hardware (Ethernet.init/begin) is brought up outside this class.

#ifndef MAX_ETH_CLIENTS
  #define MAX_ETH_CLIENTS 3   // W5100S has 4 sockets: up to 3 clients + 1 listen
#endif

class SerialEthernetInterface : public BaseSerialInterface {
  bool _isEnabled;
  bool _connected;            // true if at least one client is connected

  EthernetServer* server;
  EthernetClient  clients[MAX_ETH_CLIENTS];

  struct FrameHeader { uint8_t type; uint16_t length; };
  FrameHeader rx_header[MAX_ETH_CLIENTS];   // per-client inbound parse state

  struct Frame {
    int8_t  target;           // -1 = broadcast, else client index
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define ETH_FRAME_QUEUE_SIZE  16
  int   send_queue_len;
  Frame send_queue[ETH_FRAME_QUEUE_SIZE];

  int _last_rx;               // client index of the most recent inbound command
  int _rr;                    // round-robin cursor for fair inbound polling

public:
  SerialEthernetInterface() : server(NULL) {
    _isEnabled = false;
    _connected = false;
    send_queue_len = 0;
    _last_rx = -1;
    _rr = 0;
    for (int i = 0; i < MAX_ETH_CLIENTS; i++) { rx_header[i].type = 0; rx_header[i].length = 0; }
  }

  void begin(int port);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override { return _connected; }
  bool isWriteBusy() const override { return false; }

  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

#if ETH_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define ETH_DEBUG_PRINT(F, ...) Serial.printf("ETH: " F, ##__VA_ARGS__)
  #define ETH_DEBUG_PRINTLN(F, ...) Serial.printf("ETH: " F "\n", ##__VA_ARGS__)
#else
  #define ETH_DEBUG_PRINT(...) {}
  #define ETH_DEBUG_PRINTLN(...) {}
#endif
