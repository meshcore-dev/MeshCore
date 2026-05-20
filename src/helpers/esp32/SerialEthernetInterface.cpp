// Ethernet support for Broken Circuit Ranch POE Ethernet
// http://www.brokencircuitranch.com
// Kent Andersen

#ifdef HAS_ETHERNET

#include "SerialEthernetInterface.h"
#include <Ethernet3.h>
#include <SPI.h>

bool SerialEthernetInterface::begin(SPIClass& spi, int port, int cs_pin, int rst_pin, uint8_t mac[6]) {

  // Set CS pin before anything else
  Ethernet.setCsPin(cs_pin);

  // Reset W5500 if reset pin provided
  if (rst_pin >= 0) {
    Ethernet.setRstPin(rst_pin);
    Ethernet.hardreset();
    delay(200);
  }

  // Try DHCP
  ETH_DEBUG_PRINTLN("Starting DHCP...");
  int result = Ethernet.begin(mac);
  if (result == 0) {
    ETH_DEBUG_PRINTLN("DHCP failed, using static IP 192.168.1.200");
    IPAddress staticIP(192, 168, 1, 200);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress dns(8, 8, 8, 8);
    Ethernet.begin(mac, staticIP, subnet, gateway, dns);
  }

  ETH_DEBUG_PRINTLN("Ethernet IP: %s", Ethernet.localIP().toString().c_str());

  // Start TCP server
  server = new ConcreteEthernetServer(port);
  server->begin(port);
  _ethInitialized = true;

  ETH_DEBUG_PRINTLN("TCP server started on port %d", port);
  return true;
}

void SerialEthernetInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  clearBuffers();
}

void SerialEthernetInterface::disable() {
  _isEnabled = false;
}

size_t SerialEthernetInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    ETH_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= ETH_FRAME_QUEUE_SIZE) {
      ETH_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }
    send_queue[send_queue_len].len = len;
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;
    return len;
  }
  return 0;
}

bool SerialEthernetInterface::isWriteBusy() const {
  return false;
}

bool SerialEthernetInterface::hasReceivedFrameHeader() {
  return received_frame_header.type != 0 && received_frame_header.length != 0;
}

void SerialEthernetInterface::resetReceivedFrameHeader() {
  received_frame_header.type = 0;
  received_frame_header.length = 0;
}

size_t SerialEthernetInterface::checkRecvFrame(uint8_t dest[]) {

  if (!_ethInitialized || !server) return 0;

  // Maintain DHCP lease
  Ethernet.maintain();

  // Check for new client
  EthernetClient newClient = server->available();
  if (newClient) {
    deviceConnected = false;
    client.stop();
    client = newClient;
    resetReceivedFrameHeader();
  }

  if (client.connected()) {
    if (!deviceConnected) {
      ETH_DEBUG_PRINTLN("Client connected");
      deviceConnected = true;
    }
  } else {
    if (deviceConnected) {
      deviceConnected = false;
      ETH_DEBUG_PRINTLN("Client disconnected");
    }
  }

  if (deviceConnected) {
    if (send_queue_len > 0) {
      _last_write = millis();
      int len = send_queue[0].len;

      uint8_t pkt[3 + len];
      pkt[0] = '>';
      pkt[1] = (len & 0xFF);
      pkt[2] = (len >> 8);
      memcpy(&pkt[3], send_queue[0].buf, len);
      client.write(pkt, 3 + len);

      send_queue_len--;
      for (int i = 0; i < send_queue_len; i++) {
        send_queue[i] = send_queue[i + 1];
      }
    } else {
      if (!hasReceivedFrameHeader()) {
        if (client.available() >= 3) {
          client.readBytes(&received_frame_header.type, 1);
          client.readBytes((uint8_t*)&received_frame_header.length, 2);
        }
      }

      if (hasReceivedFrameHeader()) {
        int available = client.available();
        int frame_type = received_frame_header.type;
        int frame_length = received_frame_header.length;

        if (frame_length > available) {
          ETH_DEBUG_PRINTLN("Waiting for %d more bytes", frame_length - available);
          return 0;
        }

        if (frame_length > MAX_FRAME_SIZE) {
          ETH_DEBUG_PRINTLN("Skipping oversized frame: %d bytes", frame_length);
          while (frame_length > 0) {
            uint8_t skip[1];
            frame_length -= client.read(skip, 1);
          }
          resetReceivedFrameHeader();
          return 0;
        }

        if (frame_type != '<') {
          ETH_DEBUG_PRINTLN("Skipping unexpected frame type: 0x%x", frame_type);
          while (frame_length > 0) {
            uint8_t skip[1];
            frame_length -= client.read(skip, 1);
          }
          resetReceivedFrameHeader();
          return 0;
        }

        client.readBytes(dest, frame_length);
        resetReceivedFrameHeader();
        return frame_length;
      }
    }
  }

  return 0;
}

bool SerialEthernetInterface::isConnected() const {
  return deviceConnected;
}

#endif
