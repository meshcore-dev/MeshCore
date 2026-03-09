#include "SerialEthernetInterface.h"
#include <SPI.h>
#include <EthernetUdp.h>

#define PIN_SPI1_MISO (29) // (0 + 29)
#define PIN_SPI1_MOSI (30) // (0 + 30)
#define PIN_SPI1_SCK (3)   // (0 + 3)

SPIClass ETH_SPI_PORT(NRF_SPIM1, PIN_SPI1_MISO, PIN_SPI1_SCK, PIN_SPI1_MOSI);

#define PIN_ETH_POWER_EN WB_IO2    // output, high to enable
#define PIN_ETHERNET_RESET 21
#define PIN_ETHERNET_SS 26
//#define STATIC_IP 1

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3

bool SerialEthernetInterface::begin() {
  
  ETH_DEBUG_PRINTLN("Ethernet initalizing");

#ifdef PIN_ETH_POWER_EN
        ETH_DEBUG_PRINTLN("Ethernet power enable");
        pinMode(PIN_ETH_POWER_EN, OUTPUT);
        digitalWrite(PIN_ETH_POWER_EN, HIGH); // Power up.
        delay(100);
        ETH_DEBUG_PRINTLN("Ethernet power enabled");
#endif

#ifdef PIN_ETHERNET_RESET
        pinMode(PIN_ETHERNET_RESET, OUTPUT);
        digitalWrite(PIN_ETHERNET_RESET, LOW); // Reset Time.
        delay(100);
        digitalWrite(PIN_ETHERNET_RESET, HIGH); // Reset Time.
        ETH_DEBUG_PRINTLN("Ethernet reset pulse");
#endif

  uint8_t mac[6];
  generateDeviceMac(mac);
  ETH_DEBUG_PRINTLN(
      "Ethernet MAC: %02X:%02X:%02X:%02X:%02X:%02X",
      mac[0],
      mac[1],
      mac[2],
      mac[3],
      mac[4],
      mac[5]);
  ETH_DEBUG_PRINTLN("Init");
  ETH_SPI_PORT.begin();
  Ethernet.init(ETH_SPI_PORT, PIN_ETHERNET_SS);

  // Hardcode IP address for now
  #ifdef STATIC_IP
  IPAddress ip(192, 168, 8, 118);
  IPAddress gateway(192, 168, 8, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(192, 168, 8, 1);
  Ethernet.begin(mac, ip, dns, gateway, subnet);
  #else
  ETH_DEBUG_PRINTLN("Begin");
  if (Ethernet.begin(mac) == 0) {
    ETH_DEBUG_PRINTLN("Begin failed.");

    // DHCP failed -- let's figure out why
    if (Ethernet.hardwareStatus() == EthernetNoHardware)  // Check for Ethernet hardware present.
    {
      ETH_DEBUG_PRINTLN("Ethernet hardware not found.");
      return false;
    }
    if (Ethernet.linkStatus() == LinkOFF)     // No physical connection
    {
      ETH_DEBUG_PRINTLN("Ethernet cable not connected.");
      return false;
    }
    ETH_DEBUG_PRINTLN("Ethernet: DHCP failed for unknown reason.");
    return false;
  }
  #endif
  ETH_DEBUG_PRINTLN("Ethernet begin complete");
  IPAddress ip = Ethernet.localIP();
  ETH_DEBUG_PRINT_IP("IP", ip);
  
  IPAddress subnet = Ethernet.subnetMask();
  ETH_DEBUG_PRINT_IP("Subnet", subnet);
  
  IPAddress gateway = Ethernet.gatewayIP();
  ETH_DEBUG_PRINT_IP("Gateway", gateway);

  server.begin();   // start listening for clients
  ETH_DEBUG_PRINTLN("Ethernet: listening on TCP port: %d", TCP_PORT);

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
    ETH_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d\n", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      ETH_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

bool SerialEthernetInterface::isWriteBusy() const {
  return false;
}

size_t SerialEthernetInterface::checkRecvFrame(uint8_t dest[]) {
  // check if new client connected
  if (client && client.connected()) {
    // Avoid polling for new clients while an active connection exists.
  } else {
    auto newClient = server.available();
    if (newClient) {
      IPAddress new_ip = newClient.remoteIP();
      uint16_t new_port = newClient.remotePort();
      ETH_DEBUG_PRINTLN(
          "New client available %u.%u.%u.%u:%u",
          new_ip[0],
          new_ip[1],
          new_ip[2],
          new_ip[3],
          new_port);
      if (client && client.connected()) {
        IPAddress cur_ip = client.remoteIP();
        uint16_t cur_port = client.remotePort();
        ETH_DEBUG_PRINTLN(
            "Current client %u.%u.%u.%u:%u",
            cur_ip[0],
            cur_ip[1],
            cur_ip[2],
            cur_ip[3],
            cur_port);
        if (cur_ip == new_ip && cur_port == new_port) {
          ETH_DEBUG_PRINTLN("Ignoring duplicate client");
          return 0;
        }
      }

      deviceConnected = false;
      if (client) {
        ETH_DEBUG_PRINTLN("Closing previous client");
        client.stop();
      }
      _state = RECV_STATE_IDLE;
      _frame_len = 0;
      _rx_len = 0;
      client = newClient;
      ETH_DEBUG_PRINTLN("Switched to new client");
    }
  }

  if (client.connected()) {
    if (!deviceConnected) {
      ETH_DEBUG_PRINTLN(
          "Got connection %u.%u.%u.%u:%u",
          client.remoteIP()[0],
          client.remoteIP()[1],
          client.remoteIP()[2],
          client.remoteIP()[3],
          client.remotePort());
      deviceConnected = true;
    }
  } else {
    if (deviceConnected) {
      deviceConnected = false;
      ETH_DEBUG_PRINTLN("Disconnected");
    }
  }

  if (deviceConnected) {
    if (send_queue_len > 0) {   // first, check send queue
      
      _last_write = millis();
      int len = send_queue[0].len;

#if ETH_RAW_LINE
      ETH_DEBUG_PRINTLN("TX line len=%d", len);
      client.write(send_queue[0].buf, len);
      client.write("\r\n", 2);
#else
      uint8_t pkt[3+len]; // use same header as serial interface so client can delimit frames
      pkt[0] = '>';
      pkt[1] = (len & 0xFF);  // LSB
      pkt[2] = (len >> 8);    // MSB
      memcpy(&pkt[3], send_queue[0].buf, send_queue[0].len);
      ETH_DEBUG_PRINTLN("Sending frame len=%d", len);
      #if ETH_DEBUG_LOGGING && ARDUINO
      ETH_DEBUG_PRINTLN("TX frame len=%d", len);
      #endif
      client.write(pkt, 3 + len);
#endif
      send_queue_len--;
      for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
        send_queue[i] = send_queue[i + 1];
      }
    } else {
      while (client.available()) {
        int c = client.read();
        if (c < 0) break;

#if ETH_RAW_LINE
        if (c == '\r' || c == '\n') {
          if (_rx_len == 0) {
            continue;
          }
          uint16_t out_len = _rx_len;
          if (out_len > MAX_FRAME_SIZE) {
            out_len = MAX_FRAME_SIZE;
          }
          memcpy(dest, _rx_buf, out_len);
          _rx_len = 0;
          return out_len;
        }
        if (_rx_len < MAX_FRAME_SIZE) {
          _rx_buf[_rx_len] = (uint8_t)c;
          _rx_len++;
        }
#else
        switch (_state) {
          case RECV_STATE_IDLE:
            if (c == '<') {
              _state = RECV_STATE_HDR_FOUND;
            }
            break;
          case RECV_STATE_HDR_FOUND:
            _frame_len = (uint8_t)c;
            _state = RECV_STATE_LEN1_FOUND;
            break;
          case RECV_STATE_LEN1_FOUND:
            _frame_len |= ((uint16_t)c) << 8;
            _rx_len = 0;
            _state = _frame_len > 0 ? RECV_STATE_LEN2_FOUND : RECV_STATE_IDLE;
            break;
          default:
            if (_rx_len < MAX_FRAME_SIZE) {
              _rx_buf[_rx_len] = (uint8_t)c;
            }
            _rx_len++;
            if (_rx_len >= _frame_len) {
              if (_frame_len > MAX_FRAME_SIZE) {
                _frame_len = MAX_FRAME_SIZE;
              }
              #if ETH_DEBUG_LOGGING && ARDUINO
              ETH_DEBUG_PRINTLN("RX frame len=%d", _frame_len);
              #endif
              memcpy(dest, _rx_buf, _frame_len);
              _state = RECV_STATE_IDLE;
              return _frame_len;
            }
        }
#endif
      }
    }
  }

  return 0;
}

bool SerialEthernetInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}

void SerialEthernetInterface::generateDeviceMac(uint8_t mac[6]) {
  uint32_t device_id = NRF_FICR->DEVICEID[0];

  mac[0] = 0x02;
  mac[1] = 0x92;
  mac[2] = 0x1F;
  mac[3] = (device_id >> 16) & 0xFF;
  mac[4] = (device_id >> 8) & 0xFF;
  mac[5] = device_id & 0xFF;
}

void SerialEthernetInterface::maintain() {
  Ethernet.maintain();
}
