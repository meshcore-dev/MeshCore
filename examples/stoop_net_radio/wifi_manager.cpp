#include "wifi_manager.h"

#ifdef WIFI_SSID

#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <target.h> // board
#include <helpers/esp32/SerialWifiInterface.h> // WIFI_DEBUG_PRINTLN, pulls in WiFi.h
#include <ElegantOTA.h>
#include "server.h"

#if defined(CLIENT_WIFI_SSID)
  #include <ESPmDNS.h>
#endif

#ifndef TCP_PORT
  #define TCP_PORT 5000
#endif

SerialWifiInterface wifi_interface;
WebServer server(80);
RateLimiter rate_limiter;

static DNSServer dns_server;
static ConnectionLimiter connection_limiter;

#if defined(CLIENT_WIFI_SSID)
  #ifndef STOOP_CLIENT_WIFI_CONNECT_TIMEOUT_MS
    #define STOOP_CLIENT_WIFI_CONNECT_TIMEOUT_MS 15000
  #endif
  #ifndef STOOP_CLIENT_WIFI_RECONNECT_ATTEMPTS
    #define STOOP_CLIENT_WIFI_RECONNECT_ATTEMPTS 5
  #endif
  #ifndef STOOP_CLIENT_WIFI_RECONNECT_INTERVAL_MS
    #define STOOP_CLIENT_WIFI_RECONNECT_INTERVAL_MS 10000
  #endif

  static bool wifi_client_active = false;
  static bool wifi_needs_reconnect = false;
  static int wifi_reconnect_attempts = 0;
  static unsigned long last_wifi_reconnect_attempt = 0;
#endif

static void startWifiAp() {
#if defined(CLIENT_WIFI_SSID)
  MDNS.end();
  WiFi.disconnect(true);
#endif

  WiFi.mode(WIFI_AP);
  // For some reason, android wants the AP to be on IP 8.8.8.8 for captive portal detection to work
  IPAddress ap_ip(8, 8, 8, 8);
  WiFi.softAPConfig(ap_ip, ap_ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(WIFI_SSID, NULL, 1, false, STOOP_MAX_CONNECTED_CLIENTS);
  WIFI_DEBUG_PRINTLN("WiFi AP started");

  dns_server.start(53, "*", WiFi.softAPIP()); // redirect all DNS lookups to us

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        WIFI_DEBUG_PRINTLN("Client connected: %02X:%02X:%02X:%02X:%02X:%02X",
                           info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                           info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                           info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
        connection_limiter.connectClient(info.wifi_ap_staconnected.mac, millis());
        break;
      case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        WIFI_DEBUG_PRINTLN("Client disconnected: %02X:%02X:%02X:%02X:%02X:%02X",
                           info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
                           info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
                           info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5]);
        connection_limiter.disconnectClient(info.wifi_ap_stadisconnected.mac);
        break;
      default:
        break;
    }
  });
}

#if defined(CLIENT_WIFI_SSID)
// attempts to join CLIENT_WIFI_SSID as a station. Returns true and starts
// mDNS on success
static bool startWifiClient() {
  WIFI_DEBUG_PRINTLN("Connecting to client WiFi: %s", CLIENT_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(CLIENT_WIFI_SSID, CLIENT_WIFI_PW);

  unsigned long connect_start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - connect_start < STOOP_CLIENT_WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    WIFI_DEBUG_PRINTLN("Failed to connect to client WiFi, falling back to AP mode");
    return false;
  }

  WIFI_DEBUG_PRINTLN("Connected to client WiFi, IP=%s", WiFi.localIP().toString().c_str());

  if (MDNS.begin(CLIENT_WIFI_URL)) {
    MDNS.addService("http", "tcp", 80);
    WIFI_DEBUG_PRINTLN("mDNS responder started: http://%s.local/", CLIENT_WIFI_URL);
  } else {
    WIFI_DEBUG_PRINTLN("mDNS responder failed to start");
  }

  wifi_client_active = true;
  wifi_needs_reconnect = false;
  wifi_reconnect_attempts = 0;

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      if (wifi_client_active) {
        WIFI_DEBUG_PRINTLN("Client WiFi disconnected. Flagging for reconnect...");
        wifi_needs_reconnect = true;
      }
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      WIFI_DEBUG_PRINTLN("Client WiFi connected successfully!");
      wifi_needs_reconnect = false;
      wifi_reconnect_attempts = 0;
    }
  });

  return true;
}
#endif // CLIENT_WIFI_SSID

// If a client has been on the network too long, disconnect them to make room for new clients.
static void checkDisconnectClient() {
  uint8_t mac[6];
  while (connection_limiter.getClientDisconnect(mac, millis())) {
    uint16_t aid;
    esp_wifi_ap_get_sta_aid(mac, &aid);
    esp_err_t err = esp_wifi_deauth_sta(aid);
    WIFI_DEBUG_PRINTLN("Disconnecting client %02X:%02X:%02X:%02X:%02X:%02X due to max connection length",
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
}

void wifiManagerBegin(MultiSerialInterface& interface_manager, StdRNG& fast_rng) {
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active
  rate_limiter.begin(&fast_rng);

  bool wifi_joined_as_client = false;
#if defined(CLIENT_WIFI_SSID)
  wifi_joined_as_client = startWifiClient();
#endif
  if (!wifi_joined_as_client) {
    startWifiAp();
    WIFI_DEBUG_PRINTLN("Could not connect to WiFi client network, starting AP");
  }

  configureServer();

  wifi_interface.begin(TCP_PORT);
  interface_manager.addInterface(InterfaceType::WiFi, &wifi_interface);
}

void wifiManagerLoop() {
  server.handleClient();
  ElegantOTA.loop();

#if defined(CLIENT_WIFI_SSID)
  if (!wifi_client_active) {
    dns_server.processNextRequest();
  }
#else
  dns_server.processNextRequest();
#endif

  // TODO(Heidt) we can probably make this only check every once in awhile
  checkDisconnectClient();

#if defined(CLIENT_WIFI_SSID)
  // client WiFi dropped. Retry a few times then give up and fall back to AP mode
  if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > STOOP_CLIENT_WIFI_RECONNECT_INTERVAL_MS)) {
    last_wifi_reconnect_attempt = millis();
    wifi_reconnect_attempts++;
    if (wifi_reconnect_attempts > STOOP_CLIENT_WIFI_RECONNECT_ATTEMPTS) {
      WIFI_DEBUG_PRINTLN("Client WiFi reconnect attempts exhausted, switching to AP mode");
      wifi_client_active = false;
      wifi_needs_reconnect = false;
      startWifiAp();
    } else {
      WIFI_DEBUG_PRINTLN("Attempting client WiFi reconnect (%d/%d)...",
                         wifi_reconnect_attempts, STOOP_CLIENT_WIFI_RECONNECT_ATTEMPTS);
      WiFi.disconnect();
      WiFi.reconnect();
    }
  }
#endif
}

bool wifiClientModeActive() {
#if defined(CLIENT_WIFI_SSID)
  return wifi_client_active;
#else
  return false;
#endif
}

#endif // WIFI_SSID
