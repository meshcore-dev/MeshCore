#pragma once

#include "../SerialEthernetInterface.h"
#include <ETH.h>
#include <WiFi.h>

// Uses the PHY configuration supplied by the Arduino board variant.
class LAN8720EthernetInterface : public SerialEthernetInterface {
  WiFiServer server;
  WiFiClient client;
  bool initialized = false;
  bool listening = false;
  bool connected = false;
  IPAddress address;

public:
  bool begin();
  void loop() override;
  bool isConnected() const override { return connected; }
  int available() override { return client.available(); }
  int read() override { return client.read(); }
  size_t write(const uint8_t* buf, size_t size) override { return client.write(buf, size); }
};
