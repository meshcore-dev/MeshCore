#pragma once

#include <Arduino.h>
#include <Mesh.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/IdentityStore.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/SensorManager.h>
#include <helpers/KestrelPLI.h>
#include <RTClib.h>
#include <target.h>

#include "TrackerPrefs.h"

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "03 May 2026"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.0.0"
#endif

#define FIRMWARE_ROLE  "tracker"

#ifndef ADVERT_NAME
  #define ADVERT_NAME   "tracker"
#endif

#ifndef LORA_FREQ
  #define LORA_FREQ   915.0
#endif
#ifndef LORA_BW
  #define LORA_BW     250
#endif
#ifndef LORA_SF
  #define LORA_SF     10
#endif
#ifndef LORA_CR
  #define LORA_CR      5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  20
#endif

#define TRACKER_DEFAULT_INTERVAL_SECS   30
#define TRACKER_EMERGENCY_INTERVAL_SECS  5
#define TRACKER_MIN_INTERVAL_SECS       10
#define TRACKER_MAX_INTERVAL_SECS      300

#define TRACKER_PREFS_FILENAME   "/tracker_prefs.bin"

class TrackerMesh : public BaseChatMesh {
public:
  TrackerMesh(mesh::Radio& radio, mesh::RNG& rng,
              mesh::RTCClock& rtc, mesh::MeshTables& tables);

  void begin(FILESYSTEM* fs);
  void loop();

  // CLI entry point (called from main.cpp serial loop)
  void handleCommand(char* command, char* reply);

  // BaseChatMesh required overrides
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override {}
  ContactInfo* processAck(const uint8_t* data) override { return NULL; }
  void onContactPathUpdated(const ContactInfo& contact) override {}
  void onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override {}
  void onCommandDataRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override {}
  void onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t* sender_prefix, const char* text) override {}
  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) override { return 0; }
  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override {}
  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char* text) override {}
  void onSendTimeout() override {}
  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override { return pkt_airtime_millis * 6; }
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override { return pkt_airtime_millis * (path_len + 2); }

  // Routing
  float getAirtimeBudgetFactor() const override { return 1.0f; }
  bool allowPacketForward(const mesh::Packet* packet) override { return true; }

  mesh::LocalIdentity self_id;

private:
  FILESYSTEM*  _fs;
  TrackerPrefs _prefs;
  ChannelDetails _channel;
  bool   _channel_loaded;
  bool   _emergency;
  unsigned long _next_broadcast;

  void broadcastPLI();
  void loadPrefs();
  void savePrefs();
  void loadChannel();

  static uint8_t batteryPct();
};
