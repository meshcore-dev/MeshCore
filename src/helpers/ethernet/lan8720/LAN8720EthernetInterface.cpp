#include "LAN8720EthernetInterface.h"

bool LAN8720EthernetInterface::begin() {
  initialized = ETH.begin();
  if (!initialized) {
    Serial.println("ERROR: LAN8720 init failed");
  }
  return initialized;
}

void LAN8720EthernetInterface::loop() {
  if (!initialized) return;

  IPAddress current = ETH.localIP();
  bool ready = ETH.linkUp() && current != IPAddress(0, 0, 0, 0);
  if (listening && (!ready || current != address)) {
    client.stop();
    server.end();
    connected = false;
    listening = false;
  }
  if (!ready) return;

  if (!listening) {
    address = current;
    server.begin(ETHERNET_TCP_PORT);
    listening = true;
    ETHERNET_DEBUG_PRINTLN("Listening on %s:%d", address.toString().c_str(), ETHERNET_TCP_PORT);
  }

  auto incoming = server.available();
  if (incoming) {
    client.stop();
    client = incoming;
    // Reset queued replies as well as partial input from the previous client.
    bool enabled = isEnabled();
    SerialEthernetInterface::disable();
    SerialEthernetInterface::enable();
    if (!enabled) SerialEthernetInterface::disable();
    onClientConnected();
  }
  connected = client.connected();
}
