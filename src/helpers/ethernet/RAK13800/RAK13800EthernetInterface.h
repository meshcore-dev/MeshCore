#pragma once

#include "../SerialEthernetInterface.h"
#include <SPI.h>
#include <RAK13800_W5100S.h>

class RAK13800EthernetInterface : public SerialEthernetInterface {
  
  bool _isConnected;
  EthernetServer server;
  EthernetClient client;

  bool bringUp();   // init the W5100S, obtain an IP and start listening

#ifdef WITH_W5100S_POE
  bool _hwReady;               // true once the (deferred) bring-up has run
  unsigned long _startedAt;    // millis() at begin(); bring-up is deferred from here
#endif

  public:
    RAK13800EthernetInterface() : server(EthernetServer(ETHERNET_TCP_PORT)) {
      _isConnected = false;
    #ifdef WITH_W5100S_POE
      _hwReady = false;
      _startedAt = 0;
    #endif
    }
    
    bool begin();
    void loop() override;

    // BaseSerialInterface methods
    bool isConnected() const override;

    int available() override;
    int read() override;
    size_t write(const uint8_t *buf, size_t size) override;
};
