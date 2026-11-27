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
  // Give the RAK19018 (Silvertel) PoE converter time to latch on the current the
  // W5100S is already drawing (see variants/rak4631/W5100SPoE.cpp) before doing the
  // disruptive Ethernet-library bring-up (PHY soft-reset + blocking DHCP), which
  // otherwise collapses the marginal PoE supply at cold start.
  #ifndef ETH_POE_DEFER_MS
    #define ETH_POE_DEFER_MS 6000
  #endif
  // Static fallback if no DHCP server answers (override per network).
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

bool RAK13800EthernetInterface::begin() {
#ifdef WITH_W5100S_POE
  // Non-disruptive only: the real bring-up runs from loop() after ETH_POE_DEFER_MS.
  _startedAt = millis();
  return true;
#else
  return bringUp();
#endif
}

bool RAK13800EthernetInterface::bringUp() {

  // WB_IO2 (power enable) is already driven HIGH by early constructor
  // in RAK4631Board.cpp to support POE boot.
  // Skip hardware reset — the W5100S comes out of power-on reset cleanly,
  // and toggling reset kills the PHY link which breaks POE power.
#ifdef PIN_ETHERNET_RESET
        pinMode(PIN_ETHERNET_RESET, OUTPUT);
        digitalWrite(PIN_ETHERNET_RESET, HIGH);
#endif

  // generate mac address
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
  ETHERNET_SPI_PORT.begin();
  Ethernet.init(ETHERNET_SPI_PORT, PIN_ETHERNET_SS);

  // Use static IP if build flags are defined, otherwise DHCP
  #if defined(ETHERNET_STATIC_IP) && defined(ETHERNET_STATIC_GATEWAY) && defined(ETHERNET_STATIC_SUBNET) && defined(ETHERNET_STATIC_DNS)
  IPAddress ip(ETHERNET_STATIC_IP);
  IPAddress gateway(ETHERNET_STATIC_GATEWAY);
  IPAddress subnet(ETHERNET_STATIC_SUBNET);
  IPAddress dns(ETHERNET_STATIC_DNS);
  Ethernet.begin(mac, ip, dns, gateway, subnet);
  #elif defined(WITH_W5100S_POE)
  // Bounded DHCP with a static-IP fallback, so the node stays reachable without a
  // DHCP server and never blocks indefinitely at cold start.
  if (Ethernet.begin(mac, 12000, 4000) == 0) {
    ETHERNET_DEBUG_PRINTLN("DHCP failed, using static IP fallback");
    IPAddress ip(ETH_STATIC_IP), gw(ETH_GATEWAY), sn(ETH_SUBNET);
    Ethernet.begin(mac, ip, gw, gw, sn);
  }
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
  ETHERNET_DEBUG_PRINT_IP("DNS", Ethernet.dnsServerIP());

  server.begin();
  ETHERNET_DEBUG_PRINTLN("listening on TCP port: %d", ETHERNET_TCP_PORT);

  return true;
}

int RAK13800EthernetInterface::available() {
  return client.available();
}

int RAK13800EthernetInterface::read() {
  return client.read();
}

size_t RAK13800EthernetInterface::write(const uint8_t *buf, size_t size) {
  return client.write(buf, size);
}

bool RAK13800EthernetInterface::isConnected() const {
  return _isConnected;
}

void RAK13800EthernetInterface::loop() {
#ifdef WITH_W5100S_POE
  if (!_hwReady) {
    if (millis() - _startedAt > ETH_POE_DEFER_MS) {
      _hwReady = bringUp();
    }
    return;
  }
#endif

  Ethernet.maintain();
  
  auto newClient = server.accept();
  if (newClient) {
    IPAddress remoteIp = newClient.remoteIP();
    uint16_t remotePort = newClient.remotePort();
    ETHERNET_DEBUG_PRINTLN("New client accepted %u.%u.%u.%u:%u", remoteIp[0], remoteIp[1], remoteIp[2], remoteIp[3], remotePort);
    if (client) {
      ETHERNET_DEBUG_PRINTLN("Closing previous client");
      client.stop();
    }
    client = newClient;
    onClientConnected();
  }

  _isConnected = client.connected();

}
