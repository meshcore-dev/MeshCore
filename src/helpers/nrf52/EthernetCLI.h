#pragma once

#ifdef ETHERNET_ENABLED

#include <Arduino.h>
#include <SPI.h>
#include <RAK13800_W5100S.h>
#include <helpers/nrf52/EthernetMac.h>
#include <helpers/nrf52/EthernetWebSocket.h>
#include <helpers/nrf52/EthernetDfu.h>
#include <target.h>  // radio_driver

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

// WebSocket access to the same CLI, for browser-based tools (browsers cannot
// open raw TCP sockets). Set -D ETHERNET_WS_PORT=0 to disable.
#ifndef ETHERNET_WS_PORT
  #define ETHERNET_WS_PORT 8023
#endif

#if ETHERNET_WS_PORT == ETHERNET_TCP_PORT
  #error "ETHERNET_WS_PORT must differ from ETHERNET_TCP_PORT"
#endif

// W5100S retransmission count (RCR). socketSend() has no timeout: it only returns
// when the ACK arrives or the chip gives up and closes the socket. The chip default
// (8 retries at 200 ms base, doubling) blocks loop() for ~32 s on a dead peer, starving
// the LoRa radio; 3 retries (200+400+800+1600 ms) bounds it to ~3 s. Fine for LAN use.
#ifndef ETHERNET_RETRANSMIT_COUNT
  #define ETHERNET_RETRANSMIT_COUNT 3
#endif

#define ETHERNET_RETRY_INTERVAL_MS 30000

static EthernetServer ethernet_server(ETHERNET_TCP_PORT);
static EthernetClient ethernet_client;
static volatile bool ethernet_running = false;
static bool ethernet_session_changed = false;  // new/closed session: drop partial line
static bool ethernet_skip_line = false;        // discarding the rest of an over-long line

#if ETHERNET_WS_PORT
static EthernetServer ethernet_ws_server(ETHERNET_WS_PORT);
static EthernetClient ethernet_ws_client;   // active (upgraded) session
static EthernetClient ethernet_ws_pending;  // connected, handshake in progress
static WsHandshake ethernet_ws_handshake;
static WsSession ethernet_ws;
#endif

// Only one CLI session at a time: the newest telnet connection or completed
// WebSocket handshake wins.
static bool ethernet_ws_active() {
#if ETHERNET_WS_PORT
  return ethernet_ws.state == WS_OPEN;
#else
  return false;
#endif
}

static void ethernet_ws_close() {
#if ETHERNET_WS_PORT
  if (ethernet_ws_client) ethernet_ws_client.stop();
  ethernet_ws.reset();
#endif
}

// Write CLI output to whichever session is active
static void ethernet_write(const char* text) {
#if ETHERNET_WS_PORT
  if (ethernet_ws_active()) {
    if (ethernet_ws_client.connected()) ws_send_data(ethernet_ws_client, (const uint8_t*)text, strlen(text));
    return;
  }
#endif
  if (ethernet_client && ethernet_client.connected()) ethernet_client.print(text);
}

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
    Ethernet.setRetransmissionCount(ETHERNET_RETRANSMIT_COUNT);  // no concurrent SPI use yet
    Serial.printf("ETH: Listening on TCP port %d\n", ETHERNET_TCP_PORT);
    ethernet_server.begin();
#if ETHERNET_WS_PORT
    Serial.printf("ETH: WebSocket on port %d\n", ETHERNET_WS_PORT);
    ethernet_ws_server.begin();
#endif
    ethernet_running = true;
  }

  // DHCP succeeded, task is done
  vTaskDelete(NULL);
}

static void ethernet_start_task() {
  xTaskCreate(ethernet_task, "eth_init", 1024, NULL, 1, NULL);
}

// "eth.ota" reboots from ethernet_loop_maintain(), after the reply was sent
static bool ethernet_dfu_pending = false;
static uint32_t ethernet_dfu_requested_at = 0;

// True if 'command' is 'name', with any spaces/tabs around it
static bool ethernet_command_is(const char* command, const char* name) {
  size_t n = strlen(name);
  while (*command == ' ' || *command == '\t') command++;
  if (strncmp(command, name, n) != 0) return false;
  command += n;
  while (*command == ' ' || *command == '\t') command++;
  return *command == 0;
}

// Ethernet-only CLI commands (eth.status, eth.ota). Returns true if handled.
static bool ethernet_handle_command(const char* command, char* reply) {
  if (ethernet_command_is(command, "eth.ota")) {
    if (!ethernet_running) {
      strcpy(reply, "ERR: Ethernet not connected");
    } else {
      IPAddress ip = Ethernet.localIP();
      if (ip == IPAddress(0, 0, 0, 0)) {  // bootloader would listen on an unreachable address
        strcpy(reply, "ERR: no IP address");
        return true;
      }
      if (!ethernet_bootloader_has_dfu()) {  // e.g. stock bootloader: would just restart
        strcpy(reply, "ERR: bootloader has no Ethernet update support");
        return true;
      }
      sprintf(reply, "OK - rebooting into Ethernet update mode, %u.%u.%u.%u:%d",
          ip[0], ip[1], ip[2], ip[3], ETHERNET_DFU_PORT);
      if (!ethernet_dfu_pending) {  // reboot from the loop, after the reply is sent
        ethernet_dfu_pending = true;
        ethernet_dfu_requested_at = millis();
      }
    }
    return true;
  }
  if (strcmp(command, "eth.status") == 0) {
    if (!ethernet_running) {
      strcpy(reply, "ETH: not connected");
    } else {
      IPAddress ip = Ethernet.localIP();
#if ETHERNET_WS_PORT
      sprintf(reply, "ETH: %u.%u.%u.%u:%d ws:%d", ip[0], ip[1], ip[2], ip[3],
          ETHERNET_TCP_PORT, ETHERNET_WS_PORT);
#else
      sprintf(reply, "ETH: %u.%u.%u.%u:%d", ip[0], ip[1], ip[2], ip[3], ETHERNET_TCP_PORT);
#endif
      // Ethernet firmware update ("eth.ota"): port, or why it's unavailable
      if (ethernet_bootloader_has_dfu()) {
        sprintf(reply + strlen(reply), " ota:%d", ETHERNET_DFU_PORT);
      } else {
        strcat(reply, " ota:no-bootloader");
      }
    }
    return true;
  }
  return false;
}

// Check for new TCP client connections. A new telnet connection replaces any
// existing session; a WebSocket connection only replaces it once its handshake
// has succeeded (so a rejected or stalled connect can't kick the active session).
// Sockets: 2 listeners + 1 active + 1 pending/incoming = 4 = all W5100S sockets, for as
// long as a handshake is pending next to an active session (see ethernet_sockets_full()).
// A connection arriving in that state is still accepted (the chip turns the listening
// socket into the connection); its port is just not listening again until a socket is free.
// Use accept() (not available()) so we only see newly-accepted sockets;
// available() also returns existing connected sockets that have data, which
// would force us to disambiguate every inbound packet from a real new client.
static void ethernet_check_client() {
  // Release sockets whose peer has gone BEFORE accept() below: the library reuses a
  // closed socket (e.g. to re-listen) and a stale handle would then stop() the wrong one.
  if (ethernet_client && !ethernet_client.connected()) ethernet_client.stop();
#if ETHERNET_WS_PORT
  if (ethernet_ws_pending && !ethernet_ws_pending.connected()) ethernet_ws_pending.stop();
  if (ethernet_ws_active() && !ethernet_ws_client.connected()) ethernet_ws_close();
#endif

  auto newClient = ethernet_server.accept();
  if (newClient) {
    if (ethernet_client) ethernet_client.stop();
    ethernet_ws_close();
    ethernet_session_changed = true;
    ethernet_client = newClient;
    IPAddress ip = ethernet_client.remoteIP();
    Serial.printf("ETH: Client connected from %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
    ethernet_client.println(ETHERNET_CLI_BANNER);
  }

#if ETHERNET_WS_PORT
  auto wsClient = ethernet_ws_server.accept();
  if (wsClient) {
    if (ethernet_ws_pending) ethernet_ws_pending.stop();
    ethernet_ws_pending = wsClient;
    ethernet_ws_handshake.begin();
    IPAddress ip = ethernet_ws_pending.remoteIP();
    Serial.printf("ETH: WebSocket client from %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
  }

  if (ethernet_ws_pending) {
    int r = ws_handshake_step(ethernet_ws_handshake, ethernet_ws_pending);
    if (r < 0) {
      ethernet_ws_pending.stop();  // FIN goes out after the queued HTTP error
    } else if (r > 0) {
      if (ethernet_client) ethernet_client.stop();
      ethernet_ws_close();
      ethernet_ws_client = ethernet_ws_pending;
      ethernet_ws_pending = EthernetClient();
      ethernet_ws.reset();
      ethernet_ws.state = WS_OPEN;
      ethernet_session_changed = true;
      ethernet_write(ETHERNET_CLI_BANNER "\r\n");
    }
  }

  if (ethernet_ws_active() && !ws_receive(ethernet_ws, ethernet_ws_client)) {
    Serial.println("ETH: WebSocket closed");
    ethernet_ws_close();
  }
#endif
}

// True while the CLI holds all four W5100S sockets (see ethernet_check_client()).
static bool ethernet_sockets_full() {
#if ETHERNET_WS_PORT
  return (ethernet_client || ethernet_ws_active()) && ethernet_ws_pending;
#else
  return false;
#endif
}

// Call from loop() to maintain DHCP and check for new clients
static void ethernet_loop_maintain() {
  if (ethernet_dfu_pending && (uint32_t)(millis() - ethernet_dfu_requested_at) >= 500) {
    IPAddress ip = Ethernet.localIP(), mask = Ethernet.subnetMask(), gw = Ethernet.gatewayIP();
    uint8_t ip_b[4] = { ip[0], ip[1], ip[2], ip[3] };
    uint8_t mask_b[4] = { mask[0], mask[1], mask[2], mask[3] };
    uint8_t gw_b[4] = { gw[0], gw[1], gw[2], gw[3] };
    uint8_t mac[6];
    generateEthernetMac(mac);
    ethernet_ws_close();
    if (ethernet_client) ethernet_client.stop();
    Serial.println("ETH: rebooting into Ethernet update mode");
    Serial.flush();
    radio_driver.powerOff();  // the firmware won't be back for a while: don't leave the radio receiving
    ethernet_dfu_reboot(ip_b, mask_b, gw_b, mac);
  }
  if (ethernet_running) {
    ethernet_check_client();
    // A DHCP renewal needs a socket of its own. If it finds none, the library gives up
    // and never tries again (the lease would silently run out), so leave the renewal
    // for a later pass: the library's lease timers run on millis() and catch up, and a
    // pending handshake is gone within WS_HANDSHAKE_TIMEOUT_MS.
    if (!ethernet_sockets_full()) Ethernet.maintain();
  }
}

static int ethernet_available() {
#if ETHERNET_WS_PORT
  if (ethernet_ws_active()) return ethernet_ws.available();
#endif
  return ethernet_client.available();
}

static int ethernet_read() {
#if ETHERNET_WS_PORT
  if (ethernet_ws_active()) return ethernet_ws.read();
#endif
  return ethernet_client.read();
}

// Read a line from the Ethernet client into the command buffer.
// Returns true when a complete line is ready to process (command is null-terminated).
// The caller should process the command and then reset ethernet_command[0] = 0.
// Lines that don't fit the buffer are answered here with an error and never returned.
static bool ethernet_read_line(char* ethernet_command, size_t buf_size) {
  if (!ethernet_running) return false;
  if (ethernet_session_changed) {  // don't carry a half-typed line into a new session
    ethernet_session_changed = false;
    ethernet_skip_line = false;
    ethernet_command[0] = 0;
  }
  if (!ethernet_ws_active() && (!ethernet_client || !ethernet_client.connected())) return false;

  int elen = strlen(ethernet_command);
  if (elen >= (int)buf_size) { elen = 0; ethernet_command[0] = 0; }
  bool complete = false;
  while (!complete && ethernet_available()) {
    char c = ethernet_read();
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
      ethernet_write("\r\n  -> ERR: line too long\r\n");
      continue;
    }
    ethernet_command[elen++] = c;
    ethernet_command[elen] = 0;
  }
  if (!complete) return false;
  ethernet_write("\r\n");
  return true;
}

// Send a reply to the Ethernet client
static void ethernet_send_reply(const char* reply) {
  if (reply[0]) {
    char line[176];  // one write: each send blocks until the peer ACKs
    int n = snprintf(line, sizeof(line), "  -> %s\r\n", reply);
    if (n > 0 && n < (int)sizeof(line)) {
      ethernet_write(line);
    } else {
      ethernet_write("  -> "); ethernet_write(reply); ethernet_write("\r\n");
    }
  }
}

#endif // ETHERNET_ENABLED
