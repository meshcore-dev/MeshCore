#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
    #if defined(EXTRAFS)
      #include <CustomLFS.h>
      CustomLFS ExtraFS(0xD4000, 0x19000, 128);
      DataStore store(InternalFS, ExtraFS, rtc_clock);
    #else
      DataStore store(InternalFS, rtc_clock);
    #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #if defined(WITH_ETHERNET_COMPANION)
    #include <SPI.h>
    #include <helpers/SerialEthernetInterface.h>
    SerialEthernetInterface serial_interface;
    // Dedicated SPI for the W5100S on its own pins (SCK=3, MISO=29, MOSI=30).
    // The radio remaps the global `SPI` to the LoRa pins (43/44/45) in
    // std_init(), so the W5100S needs its own SPIM peripheral. SPIM2 is free
    // (radio uses SPIM3, Wire uses TWIM0/1).
    SPIClass eth_spi(NRF_SPIM2, 29, 3, 30);  // (SPIM, MISO=29, SCK=3, MOSI=30)
    uint8_t g_eth_mac[6] = {0};              // set in setup(), used in loop()
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
    // Fallback static IP, used only if DHCP fails (or if ETH_STATIC_ONLY is
    // set). DHCP is the default and is done deferred, after the PoE supply is
    // latched, so it no longer reboot-loops the device on cold start.
    // Override per network if needed (octets are comma-separated).
    #ifndef ETH_STATIC_IP
      #define ETH_STATIC_IP  192,168,1,50
    #endif
    #ifndef ETH_GATEWAY
      #define ETH_GATEWAY    192,168,1,1
    #endif
    #ifndef ETH_SUBNET
      #define ETH_SUBNET     255,255,255,0
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(ETHERNET_ENABLED)
    #include <helpers/nrf52/SerialEthernetInterface.h>
    SerialEthernetInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

/* WIFI RECONNECT TRACKERS */
#if defined(ESP32) && defined(WIFI_SSID)
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
#endif

#if defined(WITH_ETHERNET_COMPANION)
// Direct W5100S register write via eth_spi (proven path). Common-register
// block addresses are fixed: GAR=0x0001, SUBR=0x0005, SHAR=0x0009, SIPR=0x000F.
static void eth_wr(uint16_t a, uint8_t v) {
  eth_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(26, LOW);
  eth_spi.transfer(0xF0); eth_spi.transfer(a >> 8); eth_spi.transfer(a & 0xFF); eth_spi.transfer(v);
  digitalWrite(26, HIGH);
  eth_spi.endTransaction();
}
static void eth_write_netcfg(const uint8_t* mac) {
  const uint8_t ip[4]  = { ETH_STATIC_IP };
  const uint8_t gw[4]  = { ETH_GATEWAY };
  const uint8_t sn[4]  = { ETH_SUBNET };
  for (int i = 0; i < 4; i++) eth_wr(0x0001 + i, gw[i]);   // GAR
  for (int i = 0; i < 4; i++) eth_wr(0x0005 + i, sn[i]);   // SUBR
  for (int i = 0; i < 6; i++) eth_wr(0x0009 + i, mac[i]);  // SHAR
  for (int i = 0; i < 4; i++) eth_wr(0x000F + i, ip[i]);   // SIPR
}
#endif

void setup() {
  Serial.begin(115200);
  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#if defined(WITH_ETHERNET_COMPANION)
  {
    // Bring up the W5100S (RAK13800) TCP/IP stack so the companion protocol is
    // reachable over Ethernet (Home Assistant connects to this IP : TCP_PORT).
    // Chip power + reset is handled in board.begin() (WITH_W5100S_POE: 3V3_EN +
    // RST). The W5100S has its OWN SPI peripheral (eth_spi on SPIM2, pins
    // SCK=3/MISO=29/MOSI=30, CS=26) — separate from the radio, which uses the
    // global SPI on SPIM3 remapped to the LoRa pins. Derive a stable
    // locally-administered MAC from the nRF52 device ID.
    // Compute a stable locally-administered MAC from the nRF52 device ID.
    // IMPORTANT: the W5100S/Ethernet library bring-up (W5100.init does a PHY
    // soft-reset) is DEFERRED to loop() — see below. Doing it here in setup
    // dipped the W5100S current during the marginal PoE cold-start window and
    // collapsed the RAK19018 (Silvertel) converter → reboot loop. board.begin
    // already has the W5100S drawing current (3V3_EN + RST + bit-bang reset),
    // which latches the PoE converter just like the plain repeater build.
    g_eth_mac[0] = 0x02;  // locally administered, unicast
    uint32_t id0 = NRF_FICR->DEVICEID[0];
    uint32_t id1 = NRF_FICR->DEVICEID[1];
    g_eth_mac[1] = (id0 >> 24) & 0xFF;
    g_eth_mac[2] = (id0 >> 16) & 0xFF;
    g_eth_mac[3] = (id0 >> 8)  & 0xFF;
    g_eth_mac[4] = (id0)       & 0xFF;
    g_eth_mac[5] = (id1)       & 0xFF;

    // Non-disruptive SPI setup here (no chip reset); the disruptive part — the
    // lib's Ethernet.begin() / W5100.init() PHY soft-reset — is deferred to
    // loop() (~6 s) so it can't collapse the marginal PoE supply at cold start.
    eth_spi.begin();
    Ethernet.init(eth_spi, 26);
    Serial.println("Ethernet companion: bring-up deferred to loop()");
  }
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  the_mesh.startInterface(serial_interface);
#elif defined(ETHERNET_ENABLED)
  Serial.print("Waiting for serial to connect...\n");
  unsigned long timeout = millis();
  while (!Serial) {
    if ((millis() - timeout) < 5000) { delay(100); } else { break; }
  }
  Serial.println("Initializing Ethernet adapter...");
  if (serial_interface.begin()) {
    the_mesh.startInterface(serial_interface);
  } else {
    Serial.println("ETH: Init failed, continuing without Ethernet (mesh only)");
  }
#else
  serial_interface.begin(Serial);
  the_mesh.startInterface(serial_interface);
#endif
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
    the_mesh.startInterface(serial_interface);
#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef WIFI_SSID
  board.setInhibitSleep(true);   // prevent sleep when WiFi is active
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
      if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
          WIFI_DEBUG_PRINTLN("WiFi disconnected. Flagging for reconnect...");
          wifi_needs_reconnect = true;
      } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          WIFI_DEBUG_PRINTLN("WiFi connected successfully!");
          wifi_needs_reconnect = false;
      }
  });

  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(BLE_PIN_CODE)
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#else
  #error "need to define filesystem"
#endif

  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif

  board.onBootComplete();
}

void loop() {
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif

#ifdef ETHERNET_ENABLED
  serial_interface.loop();
#endif

  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#endif
  }

#if defined(ESP32) && defined(WIFI_SSID)
  // Safely attempt to reconnect every 10 seconds if flagged
  if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > 10000)) {
    WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
    last_wifi_reconnect_attempt = millis();
  }
#endif

#if defined(WITH_ETHERNET_COMPANION)
  // Deferred Ethernet bring-up: only AFTER the device has booted and the PoE
  // converter is solidly latched (~6 s). The W5100.init() PHY soft-reset would
  // collapse the marginal PoE supply if done during setup() (reboot loop).
  static bool _eth_up = false;
  if (!_eth_up && millis() > 6000) {
#if defined(ETH_STATIC_ONLY)
    // Static-only (opt-out of DHCP via -D ETH_STATIC_ONLY).
    IPAddress sip(ETH_STATIC_IP), sgw(ETH_GATEWAY), ssn(ETH_SUBNET);
    Ethernet.begin(g_eth_mac, sip, sgw, sgw, ssn);  // inits chip mode/sockets (PHY soft-reset)
    serial_interface.begin(TCP_PORT);               // start TCP server
    delay(50);
    eth_write_netcfg(g_eth_mac);                    // force IP/GW/SN/MAC (reliable here)
#else
    // Default: DHCP, but only HERE (deferred) where the PoE supply is already
    // latched, so the blocking DHCP exchange can't collapse the converter at
    // cold start. Bounded timeout; fall back to the static IP if no DHCP server
    // answers, so the node is always reachable and never reboot-loops. Use a
    // DHCP reservation on the router for a stable address.
    Serial.println("Ethernet: trying DHCP (deferred)...");
    int dhcp_ok = Ethernet.begin(g_eth_mac, 12000, 4000);  // 12s lease, 4s resp
    if (!dhcp_ok) {
      IPAddress sip(ETH_STATIC_IP), sgw(ETH_GATEWAY), ssn(ETH_SUBNET);
      Ethernet.begin(g_eth_mac, sip, sgw, sgw, ssn);
      delay(50);
      eth_write_netcfg(g_eth_mac);                  // force static into W5100S regs
      Serial.println("Ethernet: DHCP failed -> static IP fallback");
    }
    serial_interface.begin(TCP_PORT);               // start TCP server
#endif
    _eth_up = true;
    IPAddress ip = Ethernet.localIP();
    Serial.print("Ethernet up (deferred): ");
    Serial.print(ip[0]); Serial.print('.'); Serial.print(ip[1]); Serial.print('.');
    Serial.print(ip[2]); Serial.print('.'); Serial.print(ip[3]);
    Serial.print(":"); Serial.println(TCP_PORT);
  }
#if !defined(ETH_STATIC_ONLY)
  else if (_eth_up) {
    Ethernet.maintain();   // renew the DHCP lease in the background
  }
#endif
#endif
}
