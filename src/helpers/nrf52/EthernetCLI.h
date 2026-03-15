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

// FreeRTOS task: handles hw init, DHCP, and retries in the background
static void ethernet_task(void* param) {
  (void)param;

  Serial.println("ETH: Initializing hardware");
  pinMode(PIN_ETHERNET_POWER_EN, OUTPUT);
  digitalWrite(PIN_ETHERNET_POWER_EN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(100));

  pinMode(PIN_ETHERNET_RESET, OUTPUT);
  digitalWrite(PIN_ETHERNET_RESET, LOW);
  vTaskDelay(pdMS_TO_TICKS(100));
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

// Check for new TCP client connections, replacing any existing connection
static void ethernet_check_client() {
  auto newClient = ethernet_server.available();
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
static bool ethernet_read_line(char* ethernet_command, size_t buf_size) {
  if (!ethernet_running || !ethernet_client || !ethernet_client.connected()) return false;

  int elen = strlen(ethernet_command);
  while (ethernet_client.available() && elen < (int)buf_size - 1) {
    char c = ethernet_client.read();
    if (c == '\n' && elen == 0) continue;  // ignore leading LF (from CR+LF)
    if (c == '\r' || c == '\n') { ethernet_command[elen++] = '\r'; break; }
    ethernet_command[elen++] = c;
    ethernet_command[elen] = 0;
  }
  if (elen == (int)buf_size - 1) {
    ethernet_command[buf_size - 1] = '\r';
  }

  if (elen > 0 && ethernet_command[elen - 1] == '\r') {
    ethernet_command[elen - 1] = 0;
    ethernet_client.println();
    return true;
  }
  return false;
}

// Send a reply to the Ethernet client
static void ethernet_send_reply(const char* reply) {
  if (reply[0]) {
    ethernet_client.print("  -> "); ethernet_client.println(reply);
  }
}

#endif // ETHERNET_ENABLED
