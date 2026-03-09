#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#include "MyMesh.h"

#ifdef ETH_ENABLED
  #include <SPI.h>
  #include <RAK13800_W5100S.h>

  #define PIN_SPI1_MISO (29)
  #define PIN_SPI1_MOSI (30)
  #define PIN_SPI1_SCK  (3)
  SPIClass ETH_SPI_PORT(NRF_SPIM1, PIN_SPI1_MISO, PIN_SPI1_SCK, PIN_SPI1_MOSI);

  #define PIN_ETH_POWER_EN  WB_IO2
  #define PIN_ETHERNET_RESET 21
  #define PIN_ETHERNET_SS    26

  #ifndef ETH_TCP_PORT
    #define ETH_TCP_PORT 23
  #endif

  #define ETH_RETRY_INTERVAL_MS 30000

  static EthernetServer eth_server(ETH_TCP_PORT);
  static EthernetClient eth_client;
  static volatile bool eth_running = false;

  static void generateDeviceMac(uint8_t mac[6]) {
    uint32_t device_id = NRF_FICR->DEVICEID[0];
    mac[0] = 0x02; mac[1] = 0x92; mac[2] = 0x1F;
    mac[3] = (device_id >> 16) & 0xFF;
    mac[4] = (device_id >> 8) & 0xFF;
    mac[5] = device_id & 0xFF;
  }

  static void eth_task(void* param) {
    (void)param;

    Serial.println("ETH: Initializing hardware");
    pinMode(PIN_ETH_POWER_EN, OUTPUT);
    digitalWrite(PIN_ETH_POWER_EN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(100));

    pinMode(PIN_ETHERNET_RESET, OUTPUT);
    digitalWrite(PIN_ETHERNET_RESET, LOW);
    vTaskDelay(pdMS_TO_TICKS(100));
    digitalWrite(PIN_ETHERNET_RESET, HIGH);

    ETH_SPI_PORT.begin();
    Ethernet.init(ETH_SPI_PORT, PIN_ETHERNET_SS);

    uint8_t mac[6];
    generateDeviceMac(mac);
    Serial.printf("ETH: MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    while (!eth_running) {
      if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println("ETH: Hardware not found, giving up");
        vTaskDelete(NULL);
        return;
      }
      if (Ethernet.linkStatus() == LinkOFF) {
        vTaskDelay(pdMS_TO_TICKS(ETH_RETRY_INTERVAL_MS));
        continue;
      }

      Serial.println("ETH: Link detected, attempting DHCP...");
      if (Ethernet.begin(mac, 10000, 2000) == 0) {
        Serial.println("ETH: DHCP failed, will retry");
        vTaskDelay(pdMS_TO_TICKS(ETH_RETRY_INTERVAL_MS));
        continue;
      }

      IPAddress ip = Ethernet.localIP();
      Serial.printf("ETH: IP: %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
      Serial.printf("ETH: Listening on TCP port %d\n", ETH_TCP_PORT);
      eth_server.begin();
      eth_running = true;
    }
    vTaskDelete(NULL);
  }

  static void eth_start_task() {
    xTaskCreate(eth_task, "eth_init", 1024, NULL, 1, NULL);
  }

  static bool eth_handle_command(const char* command, char* reply) {
    if (strcmp(command, "eth") != 0) return false;
    if (!eth_running) {
      strcpy(reply, "ETH: not connected");
    } else {
      IPAddress ip = Ethernet.localIP();
      sprintf(reply, "ETH: %u.%u.%u.%u:%d", ip[0], ip[1], ip[2], ip[3], ETH_TCP_PORT);
    }
    return true;
  }

  static void eth_check_client() {
    if (eth_client && eth_client.connected()) return;
    auto newClient = eth_server.available();
    if (newClient) {
      if (eth_client) eth_client.stop();
      eth_client = newClient;
      IPAddress ip = eth_client.remoteIP();
      Serial.printf("ETH: Client connected from %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
      eth_client.println("MeshCore Room Server CLI");
      eth_client.print("> ");
    }
  }
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[MAX_POST_TEXT_LEN+1];
#ifdef ETH_ENABLED
static char eth_command[MAX_POST_TEXT_LEN+1];
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Room ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;
#ifdef ETH_ENABLED
  eth_command[0] = 0;
#endif

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#ifdef ETH_ENABLED
  eth_start_task();
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif
}

void loop() {
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    reply[0] = 0;
#ifdef ETH_ENABLED
    if (!eth_handle_command(command, reply))
#endif
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#ifdef ETH_ENABLED
  if (eth_running) {
    eth_check_client();
    Ethernet.maintain();
  }

  if (eth_running && eth_client && eth_client.connected()) {
    int elen = strlen(eth_command);
    while (eth_client.available() && elen < (int)sizeof(eth_command)-1) {
      char c = eth_client.read();
      if (c == '\n') continue;
      eth_command[elen++] = c;
      eth_command[elen] = 0;
      if (c == '\r') break;
    }
    if (elen == sizeof(eth_command)-1) {
      eth_command[sizeof(eth_command)-1] = '\r';
    }

    if (elen > 0 && eth_command[elen - 1] == '\r') {
      eth_command[elen - 1] = 0;
      eth_client.println();
      char reply[160];
      reply[0] = 0;
      if (!eth_handle_command(eth_command, reply))
        the_mesh.handleCommand(0, eth_command, reply);
      if (reply[0]) {
        eth_client.print("  -> "); eth_client.println(reply);
      }
      eth_client.print("> ");
      eth_command[0] = 0;
    }
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
}
