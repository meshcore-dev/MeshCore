#include "SerialEthernetInterface.h"
#include "EthernetMac.h"
#include <SPI.h>
#include <EthernetUdp.h>

#define PIN_SPI1_MISO (29) // (0 + 29)
#define PIN_SPI1_MOSI (30) // (0 + 30)
#define PIN_SPI1_SCK (3)   // (0 + 3)

SPIClass ETHERNET_SPI_PORT(NRF_SPIM1, PIN_SPI1_MISO, PIN_SPI1_SCK, PIN_SPI1_MOSI);

#define PIN_ETHERNET_POWER_EN WB_IO2    // output, high to enable
#define PIN_ETHERNET_RESET 21
#define PIN_ETHERNET_SS 26

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3

void SerialEthernetInterface::clearClientState(int idx) {
  if (idx < 0 || idx >= MAX_ETH_CLIENTS) {
    return;
  }
  clients[idx].stop();
  rx_header[idx].state = RECV_STATE_IDLE;
  rx_header[idx].frame_len = 0;
  rx_header[idx].rx_len = 0;
  memset(rx_header[idx].rx_buf, 0, sizeof(rx_header[idx].rx_buf));
}

static bool anyClientConnected(EthernetClient clients[]) {
  for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
    if (clients[i] && clients[i].connected()) {
      return true;
    }
  }
  return false;
}

bool SerialEthernetInterface::begin() {
  ETHERNET_DEBUG_PRINTLN("Ethernet initializing");

  // WB_IO2 (power enable) is already driven HIGH by early constructor
  // in RAK4631Board.cpp to support POE boot.
  // Skip hardware reset — the W5100S comes out of power-on reset cleanly,
  // and toggling reset kills the PHY link which breaks POE power.
#ifdef PIN_ETHERNET_RESET
  pinMode(PIN_ETHERNET_RESET, OUTPUT);
  digitalWrite(PIN_ETHERNET_RESET, HIGH);
#endif

  uint8_t mac[6];
  generateEthernetMac(mac);
  ETHERNET_DEBUG_PRINTLN(
      "Ethernet MAC: %02X:%02X:%02X:%02X:%02X:%02X",
      mac[0],
      mac[1],
      mac[2],
      mac[3],
      mac[4],
      mac[5]);
  ETHERNET_DEBUG_PRINTLN("Init");
  ETHERNET_SPI_PORT.begin();
  Ethernet.init(ETHERNET_SPI_PORT, PIN_ETHERNET_SS);

  // Use static IP if build flags are defined, otherwise DHCP
#if defined(ETHERNET_STATIC_IP) && defined(ETHERNET_STATIC_GATEWAY) && defined(ETHERNET_STATIC_SUBNET) && defined(ETHERNET_STATIC_DNS)
  IPAddress ip(ETHERNET_STATIC_IP);
  IPAddress gateway(ETHERNET_STATIC_GATEWAY);
  IPAddress subnet(ETHERNET_STATIC_SUBNET);
  IPAddress dns(ETHERNET_STATIC_DNS);
  Ethernet.begin(mac, ip, dns, gateway, subnet);
#else
  ETHERNET_DEBUG_PRINTLN("Begin");
  if (Ethernet.begin(mac) == 0) {
    ETHERNET_DEBUG_PRINTLN("Begin failed.");

    // DHCP failed -- let's figure out why
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      ETHERNET_DEBUG_PRINTLN("Ethernet hardware not found.");
      return false;
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      ETHERNET_DEBUG_PRINTLN("Ethernet cable not connected.");
      return false;
    }
    ETHERNET_DEBUG_PRINTLN("Ethernet: DHCP failed for unknown reason.");
    return false;
  }
#endif
  ETHERNET_DEBUG_PRINTLN("Ethernet begin complete");
  ETHERNET_DEBUG_PRINT_IP("IP", Ethernet.localIP());
  ETHERNET_DEBUG_PRINT_IP("Subnet", Ethernet.subnetMask());
  ETHERNET_DEBUG_PRINT_IP("Gateway", Ethernet.gatewayIP());

  server.begin();   // start listening for clients
  ETHERNET_DEBUG_PRINTLN("Ethernet: listening on TCP port: %d", ETHERNET_TCP_PORT);

  clearBuffers();
  _last_rx = -1;
  _rr = 0;
  deviceConnected = false;

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
  if (!_isEnabled || len == 0) {
    return 0;
  }

  if (len > MAX_FRAME_SIZE) {
    ETHERNET_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  const bool broadcast = src[0] >= 0x80;
  if (!broadcast && (_last_rx < 0 || _last_rx >= MAX_ETH_CLIENTS)) {
    return 0;
  }

  if (send_queue_len >= ETH_FRAME_QUEUE_SIZE) {
    ETHERNET_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
    return 0;
  }

  Frame& frame = send_queue[send_queue_len];
  frame.len = len;
  frame.target = broadcast ? -1 : (int8_t)_last_rx;
  frame.broadcast = broadcast;
  memcpy(frame.buf, src, len);
  send_queue_len++;

  return len;
}

bool SerialEthernetInterface::isWriteBusy() const {
  return false;
}

static bool parseClientByte(SerialEthernetInterface::FrameHeader& rx, uint8_t c, uint8_t dest[]) {
  switch (rx.state) {
    case RECV_STATE_IDLE:
      if (c == '<') {
        rx.state = RECV_STATE_HDR_FOUND;
      }
      break;
    case RECV_STATE_HDR_FOUND:
      rx.frame_len = (uint8_t)c;
      rx.state = RECV_STATE_LEN1_FOUND;
      break;
    case RECV_STATE_LEN1_FOUND:
      rx.frame_len |= ((uint16_t)c) << 8;
      rx.rx_len = 0;
      rx.state = rx.frame_len > 0 ? RECV_STATE_LEN2_FOUND : RECV_STATE_IDLE;
      break;
    default:
      if (rx.rx_len < MAX_FRAME_SIZE) {
        rx.rx_buf[rx.rx_len] = c;
      }
      rx.rx_len++;
      if (rx.rx_len >= rx.frame_len) {
        if (rx.frame_len > MAX_FRAME_SIZE) {
          rx.frame_len = MAX_FRAME_SIZE;
        }
        memcpy(dest, rx.rx_buf, rx.frame_len);
        rx.state = RECV_STATE_IDLE;
        return true;
      }
      break;
  }
  return false;
}

static void sendFrameToClient(EthernetClient& client, const uint8_t* frame, size_t len) {
  uint8_t pkt[3 + MAX_FRAME_SIZE];
  pkt[0] = '>';
  pkt[1] = (uint8_t)(len & 0xFF);
  pkt[2] = (uint8_t)((len >> 8) & 0xFF);
  memcpy(&pkt[3], frame, len);
  client.write(pkt, 3 + len);
}

size_t SerialEthernetInterface::checkRecvFrame(uint8_t dest[]) {
  auto newClient = server.accept();
  if (newClient) {
    IPAddress new_ip = newClient.remoteIP();
    uint16_t new_port = newClient.remotePort();
    ETHERNET_DEBUG_PRINTLN(
        "New client accepted %u.%u.%u.%u:%u",
        new_ip[0],
        new_ip[1],
        new_ip[2],
        new_ip[3],
        new_port);

    int slot = -1;
    for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
      if (!clients[i] || !clients[i].connected()) {
        slot = i;
        break;
      }
    }

    if (slot >= 0) {
      clients[slot].stop();
      clients[slot] = newClient;
      rx_header[slot].state = RECV_STATE_IDLE;
      rx_header[slot].frame_len = 0;
      rx_header[slot].rx_len = 0;
      _last_rx = slot;
      deviceConnected = true;
      ETHERNET_DEBUG_PRINTLN("Accepted client in slot %d", slot);
    } else {
      ETHERNET_DEBUG_PRINTLN("All client slots full, rejecting new connection");
      newClient.stop();
    }
  }

  deviceConnected = anyClientConnected(clients);

  for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
    if (!clients[i]) {
      continue;
    }
    if (!clients[i].connected()) {
      if (_last_rx == i) {
        _last_rx = -1;
      }
      clearClientState(i);
      continue;
    }
  }

  if (!deviceConnected) {
    return 0;
  }

  if (send_queue_len > 0) {
    _last_write = millis();
    Frame& frame = send_queue[0];
    bool sent = false;

    if (frame.broadcast) {
      for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
        if (clients[i] && clients[i].connected()) {
          sendFrameToClient(clients[i], frame.buf, frame.len);
          sent = true;
        }
      }
    } else if (frame.target >= 0 && frame.target < MAX_ETH_CLIENTS && clients[frame.target] && clients[frame.target].connected()) {
      sendFrameToClient(clients[frame.target], frame.buf, frame.len);
      sent = true;
    } else {
      ETHERNET_DEBUG_PRINTLN("Dropping queued frame for disconnected client");
    }

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {
      send_queue[i] = send_queue[i + 1];
    }

    if (!sent) {
      return 0;
    }
  }

  for (int offset = 0; offset < MAX_ETH_CLIENTS; offset++) {
    int idx = (_rr + offset) % MAX_ETH_CLIENTS;
    if (!clients[idx] || !clients[idx].connected()) {
      continue;
    }
    while (clients[idx].available()) {
      int c = clients[idx].read();
      if (c < 0) break;
      if (parseClientByte(rx_header[idx], (uint8_t)c, dest)) {
        _last_rx = idx;
        _rr = (idx + 1) % MAX_ETH_CLIENTS;
        return rx_header[idx].frame_len;
      }
    }
  }

  return 0;
}

bool SerialEthernetInterface::isConnected() const {
  return deviceConnected;
}

void SerialEthernetInterface::loop() {
  Ethernet.maintain();
}
