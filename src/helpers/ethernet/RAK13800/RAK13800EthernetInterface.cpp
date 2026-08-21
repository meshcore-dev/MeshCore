#include "RAK13800EthernetInterface.h"
#include "../../nrf52/EthernetMac.h"
#include <SPI.h>
#include <EthernetUdp.h>

#define PIN_SPI1_MISO (29) // (0 + 29)
#define PIN_SPI1_MOSI (30) // (0 + 30)
#define PIN_SPI1_SCK (3)   // (0 + 3)

SPIClass ETHERNET_SPI_PORT(NRF_SPIM1, PIN_SPI1_MISO, PIN_SPI1_SCK, PIN_SPI1_MOSI);

#define PIN_ETHERNET_POWER_EN WB_IO2    // output, high to enable
#define PIN_ETHERNET_RESET 21
#define PIN_ETHERNET_SS 26

#ifdef WITH_W5100S_POE
  // Give the RAK19018 (Silvertel) PoE converter time to latch on the current
  // the W5100S is already drawing (board.begin()'s early RST release + bit-bang
  // soft-reset) before doing the *disruptive* Ethernet-library bring-up (another
  // PHY soft-reset + blocking DHCP) — doing that immediately reliably collapsed
  // the marginal PoE supply.
  #ifndef ETH_POE_DEFER_MS
    #define ETH_POE_DEFER_MS 6000
  #endif
  #ifndef ETH_STATIC_IP
    #define ETH_STATIC_IP  192,168,1,50
  #endif
  #ifndef ETH_GATEWAY
    #define ETH_GATEWAY    192,168,1,1
  #endif
  #ifndef ETH_SUBNET
    #define ETH_SUBNET     255,255,255,0
  #endif
#endif

static void eth_init_spi_and_pins() {
  // WB_IO2 (power enable) is already driven HIGH by early constructor
  // in RAK4631Board.cpp to support POE boot.
  // Skip hardware reset — the W5100S comes out of power-on reset cleanly,
  // and toggling reset kills the PHY link which breaks POE power.
#ifdef PIN_ETHERNET_RESET
  pinMode(PIN_ETHERNET_RESET, OUTPUT);
  digitalWrite(PIN_ETHERNET_RESET, HIGH);
#endif

  ETHERNET_SPI_PORT.begin();
  Ethernet.init(ETHERNET_SPI_PORT, PIN_ETHERNET_SS);
}

// Bring up the DHCP/static IP + start listening. Returns true on success.
static bool eth_bring_up(uint8_t mac[6]) {
#ifdef WITH_W5100S_POE
  // Bounded DHCP with a static-IP fallback, so the node stays reachable even
  // without a DHCP server and never blocks indefinitely at cold start.
  ETHERNET_DEBUG_PRINTLN("Trying DHCP (deferred)...");
  if (Ethernet.begin(mac, 12000, 4000) == 0) {
    ETHERNET_DEBUG_PRINTLN("DHCP failed -> static IP fallback");
    IPAddress ip(ETH_STATIC_IP), gw(ETH_GATEWAY), sn(ETH_SUBNET);
    Ethernet.begin(mac, ip, gw, gw, sn);
  }
#elif defined(ETHERNET_STATIC_IP) && defined(ETHERNET_STATIC_GATEWAY) && defined(ETHERNET_STATIC_SUBNET) && defined(ETHERNET_STATIC_DNS)
  IPAddress ip(ETHERNET_STATIC_IP);
  IPAddress gateway(ETHERNET_STATIC_GATEWAY);
  IPAddress subnet(ETHERNET_STATIC_SUBNET);
  IPAddress dns(ETHERNET_STATIC_DNS);
  Ethernet.begin(mac, ip, dns, gateway, subnet);
#else
  if (Ethernet.begin(mac) == 0) {
    ETHERNET_DEBUG_PRINTLN("Failed to initialize RAK13800 hardware.");
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      ETHERNET_DEBUG_PRINTLN("Ethernet hardware not found.");
    } else if (Ethernet.linkStatus() == LinkOFF) {
      ETHERNET_DEBUG_PRINTLN("Ethernet cable not connected.");
    } else {
      ETHERNET_DEBUG_PRINTLN("DHCP failed for unknown reason.");
    }
    return false;
  }
#endif

  ETHERNET_DEBUG_PRINTLN("Ethernet begin complete");
  ETHERNET_DEBUG_PRINT_IP("IP Address", Ethernet.localIP());
  ETHERNET_DEBUG_PRINT_IP("Subnet Mask", Ethernet.subnetMask());
  ETHERNET_DEBUG_PRINT_IP("Gateway", Ethernet.gatewayIP());
  return true;
}

bool RAK13800EthernetInterface::begin() {
  uint8_t mac[6];
  generateEthernetMac(mac);
  ETHERNET_DEBUG_PRINTLN("Ethernet MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

#ifdef WITH_W5100S_POE
  // Non-disruptive only: no Ethernet.init()/begin() here (that resets the
  // PHY and can collapse the marginal PoE supply at cold start). The real
  // bring-up runs from loop() after ETH_POE_DEFER_MS.
  _startedAt = millis();
  ETHERNET_DEBUG_PRINTLN("Ethernet bring-up deferred (PoE-safe)");
  return true;
#else
  eth_init_spi_and_pins();
  if (!eth_bring_up(mac)) return false;
  server.begin();
  ETHERNET_DEBUG_PRINTLN("listening on TCP port: %d", ETHERNET_TCP_PORT);
  return true;
#endif
}

#ifdef WITH_W5100S_POE
bool RAK13800EthernetInterface::bringUpHardware() {
  eth_init_spi_and_pins();
  uint8_t mac[6];
  generateEthernetMac(mac);
  if (!eth_bring_up(mac)) return false;
  server.begin();
  ETHERNET_DEBUG_PRINTLN("listening on TCP port: %d", ETHERNET_TCP_PORT);
  return true;
}
#endif

void RAK13800EthernetInterface::enable() {
  if (_isEnabled) return;
  _isEnabled = true;
  clearBuffers();
}

void RAK13800EthernetInterface::disable() {
  _isEnabled = false;
}

size_t RAK13800EthernetInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    ETHERNET_DEBUG_PRINTLN("writeFrame(): frame too big, len=%d", (int)len);
    return 0;
  }
#ifdef WITH_W5100S_POE
  if (!_hwReady) return 0;
#endif
  if (!_connected || len == 0) return 0;

  if (send_queue_len >= ETH_FRAME_QUEUE_SIZE) {
    ETHERNET_DEBUG_PRINTLN("writeFrame(): send_queue full (dropping code=0x%02x)", src[0]);
    return 0;
  }

  // PUSH codes (>= 0x80) go to all clients; command responses go to the
  // client that issued the most recent command.
  int8_t target = (src[0] >= 0x80) ? -1 : (int8_t)_last_rx;

  send_queue[send_queue_len].target = target;
  send_queue[send_queue_len].len = (uint8_t)len;
  memcpy(send_queue[send_queue_len].buf, src, len);
  send_queue_len++;
  return len;
}

size_t RAK13800EthernetInterface::checkRecvFrame(uint8_t dest[]) {
#ifdef WITH_W5100S_POE
  if (!_hwReady) return 0;
#endif

  // ---- accept a new connection into a free slot --------------------------
  // accept() returns each new connection once and maintains the listen socket,
  // so it must be called every loop.
  EthernetClient nc = server.accept();
  if (nc) {
    int slot = -1;
    for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
      if (!clients[i].connected()) { slot = i; break; }
    }
    if (slot >= 0) {
      clients[slot].stop();            // free any lingering socket in this slot
      clients[slot] = nc;
      rx_header[slot].type = 0;
      rx_header[slot].length = 0;
      ETHERNET_DEBUG_PRINTLN("Got connection (slot %d)", slot);
    } else {
      nc.stop();                       // all slots busy — reject
      ETHERNET_DEBUG_PRINTLN("Rejected connection (all %d slots busy)", MAX_ETH_CLIENTS);
    }
  }

  // ---- refresh connected state, free dropped sockets ---------------------
  bool any = false;
  for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
    if (clients[i].connected()) {
      any = true;
    } else if (rx_header[i].type || rx_header[i].length) {
      // a client that was active just dropped — reset its parse state
      rx_header[i].type = 0;
      rx_header[i].length = 0;
      clients[i].stop();
      ETHERNET_DEBUG_PRINTLN("Disconnected (slot %d)", i);
    }
  }
  _connected = any;

  // ---- drain the outbound queue ------------------------------------------
  while (send_queue_len > 0) {
    Frame &f = send_queue[0];
    uint8_t pkt[3 + MAX_FRAME_SIZE];
    pkt[0] = '>';
    pkt[1] = (f.len & 0xFF);
    pkt[2] = (f.len >> 8);
    memcpy(&pkt[3], f.buf, f.len);

    if (f.target < 0) {                          // broadcast (push)
      for (int i = 0; i < MAX_ETH_CLIENTS; i++) {
        if (clients[i].connected()) clients[i].write(pkt, 3 + f.len);
      }
    } else if (f.target < MAX_ETH_CLIENTS && clients[f.target].connected()) {
      clients[f.target].write(pkt, 3 + f.len);   // response to the requester
    }

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) send_queue[i] = send_queue[i + 1];
  }

  // ---- read ONE inbound frame (round-robin across clients) ---------------
  for (int k = 0; k < MAX_ETH_CLIENTS; k++) {
    int i = (_rr + k) % MAX_ETH_CLIENTS;
    EthernetClient &c = clients[i];
    if (!c.connected()) continue;

    // frame header = [type][len_lo][len_hi]
    if (rx_header[i].type == 0 || rx_header[i].length == 0) {
      if (c.available() >= 3) {
        c.readBytes(&rx_header[i].type, 1);
        c.readBytes((uint8_t *)&rx_header[i].length, 2);
      }
    }

    if (rx_header[i].type != 0 && rx_header[i].length != 0) {
      int avail = c.available();
      int frame_type = rx_header[i].type;
      int frame_length = rx_header[i].length;

      if (frame_length > avail) continue;        // wait for the rest

      if (frame_length > MAX_FRAME_SIZE || frame_type != '<') {
        // oversized or unexpected type — discard
        while (frame_length > 0) {
          uint8_t skip[1];
          int n = c.read(skip, 1);
          if (n <= 0) break;
          frame_length -= n;
        }
        rx_header[i].type = 0;
        rx_header[i].length = 0;
        continue;
      }

      c.readBytes(dest, frame_length);
      rx_header[i].type = 0;
      rx_header[i].length = 0;
      _last_rx = i;                              // route responses back here
      _rr = (i + 1) % MAX_ETH_CLIENTS;           // fairness
      ETHERNET_DEBUG_PRINTLN("RX[%d] cmd=0x%02x len=%d", i, dest[0], frame_length);
      return frame_length;
    }
  }

  return 0;
}

void RAK13800EthernetInterface::loop() {
#ifdef WITH_W5100S_POE
  if (!_hwReady) {
    if (millis() - _startedAt > ETH_POE_DEFER_MS) {
      _hwReady = bringUpHardware();
    }
    return;
  }
#endif
  Ethernet.maintain();
}
