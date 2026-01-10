#include <Arduino.h>
#include <Mesh.h>
#include <PubSubClient.h>
#include <WiFi.h>

#if !defined(ESP32)
#error "Remote telemetry example requires an ESP32 target with WiFi support"
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

#ifndef REMOTE_TELEMETRY_DEBUG
#define REMOTE_TELEMETRY_DEBUG 1
#endif

#include "RemoteTelemetryMesh.h"
#include "RemoteTelemetryManager.h"

StdRNG fast_rng;
SimpleMeshTables tables;
RemoteTelemetryMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
RemoteTelemetryManager telemetryManager(the_mesh, mqttClient);

static void halt() {
  while (true) {
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

#if REMOTE_TELEMETRY_DEBUG
  Serial.println("Remote telemetry node booting...");
#endif

  board.begin();

  if (!radio_init()) {
#if REMOTE_TELEMETRY_DEBUG
    Serial.println("Radio init failed");
#endif
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif

#if !defined(RP2040_PLATFORM)
  store.begin();
#endif

  if (!store.load("_remote", the_mesh.self_id)) {
#if REMOTE_TELEMETRY_DEBUG
    Serial.println("Generating new identity");
#endif
    the_mesh.self_id = radio_new_identity();
    int guard = 0;
    while (guard < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
      the_mesh.self_id = radio_new_identity();
      guard++;
    }
    store.save("_remote", the_mesh.self_id);
  }

#if REMOTE_TELEMETRY_DEBUG
  Serial.print("Node ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
  Serial.println();
#endif

  sensors.begin();

  radio_set_params(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR);
  radio_set_tx_power(LORA_TX_POWER);

  telemetryManager.begin();

  the_mesh.sendSelfAdvertisement(16000);
}

void loop() {
  the_mesh.loop();
  telemetryManager.loop();
  mqttClient.loop();
  sensors.loop();
  rtc_clock.tick();
}
