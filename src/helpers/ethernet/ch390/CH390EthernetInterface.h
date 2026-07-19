#pragma once

#include "../SerialEthernetInterface.h"
#include <SPI.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <ESP32_CH390.h>

class CH390EthernetInterface : public SerialEthernetInterface {
  
  bool _isConnected;
  WiFiServer server;
  WiFiClient client;

  public:
    CH390EthernetInterface(){
      _isConnected = false;
    }
    
    bool begin();
    void loop() override;

    // BaseSerialInterface methods
    bool isConnected() const override;

    int available() override;
    int read() override;
    size_t write(const uint8_t *buf, size_t size) override;
};
