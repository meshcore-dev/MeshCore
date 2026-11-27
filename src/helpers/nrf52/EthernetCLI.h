#pragma once

#ifdef ETHERNET_ENABLED

#include <Arduino.h>
#include <SPI.h>
#include <RAK13800_W5100S.h>
#include <helpers/nrf52/EthernetMac.h>

#define PIN_SPI1_MISO (29)
#define PIN_SPI1_MOSI (30)
#define PIN_SPI1_SCK  (3)

static SPIClass ETHERNET_SPI_PORT(NRF_SPIM1, PIN_SPI1_MISO, PIN_SPI1_SCK, PIN_SPI1_MOSI);

#define PIN_ETHERNET_POWER_EN  WB_IO2
#define PIN_ETHERNET_RESET 21
#define PIN_ETHERNET_SS    26

#ifndef ETHERNET_TCP_PORT
  #define ETHERNET_TCP_PORT 23  // telnet port for CLI access
#endif

#ifndef ETHERNET_CLI_BANNER
  #define ETHERNET_CLI_BANNER "MeshCore CLI"
#endif

#define ETHERNET_RETRY_INTERVAL_MS 30000

static EthernetServer ethernet_server(ETHERNET_TCP_PORT);
static EthernetClient ethernet_client;
static volatile bool ethernet_running = false;
static bool ethernet_skip_line = false;  // discarding the rest of an over-long line

// FreeRTOS task: handles hw init, DHCP, and retries in the background
static void ethernet_task(void* param) {
  (void)param;

  Serial.println("ETH: Initializing hardware");
  // WB_IO2 (power enable) is already driven HIGH by early constructor
  // in RAK4631Board.cpp to support POE boot.
  // Skip hardware reset — the W5100S comes out of power-on reset cleanly,
  // and toggling reset kills the PHY link which breaks POE power.
  pinMode(PIN_ETHERNET_RESET, OUTPUT);
  digitalWrite(PIN_ETHERNET_RESET, HIGH);

  ETHERNET_SPI_PORT.begin();
  Ethernet.init(ETHERNET_SPI_PORT, PIN_ETHERNET_SS);

  uint8_t mac[6];
  generateEthernetMac(mac);
  Serial.printf("ETH: MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Retry loop: keep trying until we get an IP
  while (!ethernet_running) {
    Serial.println("ETH: Attempting DHCP...");
    if (Ethernet.begin(mac, 10000, 2000) == 0) {
      if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println("ETH: Hardware not found, giving up");
        vTaskDelete(NULL);
        return;
      }
      if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("ETH: Cable not connected, will retry");
      } else {
        Serial.println("ETH: DHCP failed, will retry");
      }
      vTaskDelay(pdMS_TO_TICKS(ETHERNET_RETRY_INTERVAL_MS));
      continue;
    }

    IPAddress ip = Ethernet.localIP();
    Serial.printf("ETH: IP: %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("ETH: Listening on TCP port %d\n", ETHERNET_TCP_PORT);
    ethernet_server.begin();
    ethernet_running = true;
  }

  // DHCP succeeded, task is done
  vTaskDelete(NULL);
}

static void ethernet_start_task() {
  xTaskCreate(ethernet_task, "eth_init", 1024, NULL, 1, NULL);
}

// Format ethernet status into reply buffer. Returns true if command was handled.
static bool ethernet_handle_command(const char* command, char* reply) {
  if (strcmp(command, "eth.status") == 0) {
    if (!ethernet_running) {
      strcpy(reply, "ETH: not connected");
    } else {
      IPAddress ip = Ethernet.localIP();
      sprintf(reply, "ETH: %u.%u.%u.%u:%d", ip[0], ip[1], ip[2], ip[3], ETHERNET_TCP_PORT);
    }
    return true;
  }
  return false;
}

// Check for new TCP client connections, replacing any existing connection.
// Use accept() (not available()) so we only see newly-accepted sockets;
// available() also returns existing connected sockets that have data, which
// would force us to disambiguate every inbound packet from a real new client.
static void ethernet_check_client() {
  auto newClient = ethernet_server.accept();
  if (newClient) {
    if (ethernet_client) ethernet_client.stop();
    ethernet_client = newClient;
    IPAddress ip = ethernet_client.remoteIP();
    Serial.printf("ETH: Client connected from %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
    ethernet_client.println(ETHERNET_CLI_BANNER);
  }
}

// Call from loop() to maintain DHCP and check for new clients
static void ethernet_loop_maintain() {
  if (ethernet_running) {
    ethernet_check_client();
    Ethernet.maintain();
  }
}

// Read a line from the Ethernet client into the command buffer.
// Returns true when a complete line is ready to process (command is null-terminated).
// The caller should process the command and then reset ethernet_command[0] = 0.
// Lines that don't fit the buffer are answered here with an error and never returned.
static bool ethernet_read_line(char* ethernet_command, size_t buf_size) {
  if (!ethernet_running || !ethernet_client || !ethernet_client.connected()) return false;

  int elen = strlen(ethernet_command);
  if (elen >= (int)buf_size) { elen = 0; ethernet_command[0] = 0; }
  bool complete = false;
  while (!complete && ethernet_client.available()) {
    char c = ethernet_client.read();
    if (ethernet_skip_line) {
      if (c == '\r' || c == '\n') ethernet_skip_line = false;
      continue;
    }
    if (c == '\n' && elen == 0) continue;  // ignore leading LF (from CR+LF)
    if (c == '\r' || c == '\n') { complete = true; break; }
    if (elen >= (int)buf_size - 1) {  // doesn't fit: refuse it, a truncated command must not run
      elen = 0;
      ethernet_command[0] = 0;
      ethernet_skip_line = true;
      ethernet_client.print("\r\n  -> ERR: line too long\r\n");
      continue;
    }
    ethernet_command[elen++] = c;
    ethernet_command[elen] = 0;
  }
  if (!complete) return false;
  ethernet_client.println();
  return true;
}

// Send a reply to the Ethernet client
static void ethernet_send_reply(const char* reply) {
  if (reply[0]) {
    ethernet_client.print("  -> "); ethernet_client.println(reply);
  }
}

#endif // ETHERNET_ENABLED
