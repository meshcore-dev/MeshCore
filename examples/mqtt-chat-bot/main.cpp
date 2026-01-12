#include <Arduino.h>
#include <Mesh.h>
#include <WiFi.h>
#include <PubSubClient.h>

#if !defined(ESP32)
#error "MQTT chat bot example requires an ESP32 target with WiFi support"
#endif

#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/ArduinoHelpers.h>
#include <Utils.h>
#include <target.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

#include "ChatBotConfig.h"
#include "ChatBotPortal.h"
#include "ChatBotMesh.h"
#include "ChatBotManager.h"

namespace {
constexpr char IDENTITY_SLOT[] = "_chatbot";
constexpr char CONFIG_PATH[] = "/chatbot.json";
constexpr unsigned int NODE_SUFFIX_BYTES = 4;
constexpr unsigned int USERNAME_SUFFIX_BYTES = 4;

String buildHexSuffix(const uint8_t* data, size_t len) {
  static const char HEX_DIGITS_UPPER[] = "0123456789ABCDEF";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += HEX_DIGITS_UPPER[(data[i] >> 4) & 0x0F];
    out += HEX_DIGITS_UPPER[data[i] & 0x0F];
  }
  return out;
}

void halt() {
  while (true) {
    delay(1000);
  }
}
}

StdRNG fast_rng;
SimpleMeshTables tables;
ChatBotMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
chatbot::Settings botSettings = []() {
  chatbot::Settings defaults;
  defaults.applyDefaults();
  return defaults;
}();
ChatBotManager botManager(the_mesh, mqttClient, botSettings);
chatbot::ConfigStore botConfigStore(SPIFFS, CONFIG_PATH);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[chatbot] Booting MQTT bridge node...");

  board.begin();

  bool forcePortal = false;
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  delay(10);
  if (digitalRead(PIN_USER_BTN) == LOW) {
    forcePortal = true;
    Serial.println("[chatbot] Configuration reset requested");
  }
#endif

  if (!radio_init()) {
    Serial.println("[chatbot] Radio init failed");
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  IdentityStore store(InternalFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#elif defined(ESP32)
  SPIFFS.begin(true);
  IdentityStore store(SPIFFS, "/identity");
#else
  #error "Filesystem not defined for this platform"
#endif

#if !defined(RP2040_PLATFORM)
  store.begin();
#endif

  if (!store.load(IDENTITY_SLOT, the_mesh.self_id)) {
    Serial.println("[chatbot] Generating new identity");
    the_mesh.self_id = radio_new_identity();
    int guard = 0;
    while (guard < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
      the_mesh.self_id = radio_new_identity();
      guard++;
    }
    store.save(IDENTITY_SLOT, the_mesh.self_id);
  }

  bool configLoaded = botConfigStore.load();
  if (!configLoaded) {
    botConfigStore.applyDefaults();
  }

  chatbot::Settings& settings = botConfigStore.data();

  String nodeSuffix = buildHexSuffix(the_mesh.self_id.pub_key, NODE_SUFFIX_BYTES);
  if (settings.meshNodeName.length() == 0) {
    settings.meshNodeName = "IT-bot-" + nodeSuffix;
  }

  String userSuffix = buildHexSuffix(the_mesh.self_id.pub_key, USERNAME_SUFFIX_BYTES);
  if (settings.mqttUsername.length() == 0) {
    settings.mqttUsername = "chatbot-" + userSuffix;
  }

  if (forcePortal) {
    botConfigStore.clearSecrets();
    settings.wifiSsid = "ssid";
    Serial.println("[chatbot] Clearing stored credentials");
    WiFi.disconnect(true, true);
  }

  ChatBotPortal portal(botConfigStore);
  if (!portal.ensureConfigured(forcePortal || !configLoaded)) {
    Serial.println("[chatbot] Configuration portal aborted");
    halt();
  }

  if (!botConfigStore.save()) {
    Serial.println("[chatbot] Warning: failed to persist configuration");
  }

  botManager.attachConfigStore(botConfigStore);

  if (!the_mesh.configureChannel(settings.meshChannelName, settings.meshChannelKey)) {
    Serial.println("[chatbot] Failed to configure mesh channel");
    halt();
  }

  sensors.begin();

  radio_set_params(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR);
  radio_set_tx_power(LORA_TX_POWER);

  botManager.begin();

  Serial.printf("[chatbot] Node ready as %s\n", settings.meshNodeName.c_str());
}

void loop() {
  the_mesh.loop();
  botManager.loop();
  sensors.loop();
  rtc_clock.tick();
}
