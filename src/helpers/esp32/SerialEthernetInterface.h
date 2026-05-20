#pragma once
// Ethernet support for Broken Circuit Ranch POE Ethernet
// http://www.brokencircuitranch.com
// Kent Andersen

#ifdef HAS_ETHERNET

#include "../BaseSerialInterface.h"
#include <Ethernet3.h>
#include <SPI.h>

// Workaround: ESP32 Arduino core Server.h declares begin(uint16_t) as pure virtual
// but Ethernet3 only implements begin() with no args.
// This subclass satisfies the compiler by implementing the missing override.
class ConcreteEthernetServer : public EthernetServer {
public:
  ConcreteEthernetServer(uint16_t port) : EthernetServer(port) {}
  void begin(uint16_t port) override { EthernetServer::begin(); }
};

class SerialEthernetInterface : public BaseSerialInterface {
  bool deviceConnected;
  bool _isEnabled;
  bool _ethInitialized;
  unsigned long _last_write;

  ConcreteEthernetServer* server;
  EthernetClient client;

  struct FrameHeader {
    uint8_t type;
    uint16_t length;
  };

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  FrameHeader received_frame_header;

  #define ETH_FRAME_QUEUE_SIZE 4
  int recv_queue_len;
  Frame recv_queue[ETH_FRAME_QUEUE_SIZE];
  int send_queue_len;
  Frame send_queue[ETH_FRAME_QUEUE_SIZE];

  void clearBuffers() { recv_queue_len = 0; send_queue_len = 0; }

public:
  SerialEthernetInterface() : server(nullptr), client(EthernetClient()) {
    deviceConnected = false;
    _isEnabled = false;
    _ethInitialized = false;
    _last_write = 0;
    send_queue_len = recv_queue_len = 0;
    received_frame_header.type = 0;
    received_frame_header.length = 0;
  }

  // spi: shared SPI bus (vspi from target.cpp)
  // port: TCP port to listen on
  // cs_pin: W5500 chip select pin
  // rst_pin: W5500 reset pin (-1 if not used)
  // mac: 6-byte MAC address
  bool begin(SPIClass& spi, int port, int cs_pin, int rst_pin, uint8_t mac[6]);

  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;
  bool isWriteBusy() const override;

  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

  bool hasReceivedFrameHeader();
  void resetReceivedFrameHeader();
  bool isEthernetInitialized() const { return _ethInitialized; }
};

#if ETH_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define ETH_DEBUG_PRINT(F, ...) Serial.printf("ETH: " F, ##__VA_ARGS__)
  #define ETH_DEBUG_PRINTLN(F, ...) Serial.printf("ETH: " F "\n", ##__VA_ARGS__)
#else
  #define ETH_DEBUG_PRINT(...) {}
  #define ETH_DEBUG_PRINTLN(...) {}
#endif

#endif
