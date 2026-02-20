#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <RTClib.h>
#include <target.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

#include <helpers/ArduinoHelpers.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#ifndef LORA_FREQ
#define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
#define LORA_BW 250
#endif
#ifndef LORA_SF
#define LORA_SF 10
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 20
#endif

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE "19 Feb 2026"
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v1.0.0-bap"
#endif

#define FIRMWARE_ROLE "bap"

/* -------------------------------------------------------------------------------------- */

// BusArrival packet structure (~31 bytes per arrival)
struct BusArrival {
  uint32_t stop_id;         // 4 bytes - Which stop this is for
  char route[6];            // 6 bytes - e.g., "14", "38R", "N-Jud"
  char destination[12];     // 12 bytes - e.g., "Daly City", "Fremont"
  int16_t minutes;          // 2 bytes - Minutes until arrival (-1 = N/A, -2 = delayed)
  uint8_t status;           // 1 byte - 0=on time, 1=delayed, 2=cancelled
  uint32_t timestamp;       // 4 bytes - Unix timestamp for staleness
  uint8_t agency_id;        // 1 byte - SF=1, AC=2, BA=3, etc.
  uint8_t reserved;         // 1 byte - Padding/alignment
};                          // Total: 31 bytes per arrival

// BAPArrivalPacket - fits 5 arrivals in 184-byte payload
struct BAPArrivalPacket {
  uint8_t packet_type;      // 1 byte - Always 0x01 for arrival data
  uint8_t count;            // 1 byte - Number of arrivals (max 5)
  uint16_t sequence;        // 2 bytes - Sequence number for dedup
  uint32_t generated_at;    // 4 bytes - When gateway fetched this data
  BusArrival arrivals[5];   // 155 bytes - Up to 5 arrivals
  uint8_t checksum;         // 1 byte - Simple checksum
};                          // Total: 164 bytes

// BAP Configuration stored in SPIFFS
struct BAPConfig {
  uint8_t node_role;        // 0=auto, 1=gateway, 2=display-only
  uint32_t stop_id;         // Transit stop ID
  char wifi_ssid[32];
  char wifi_password[64];
  uint8_t is_repeater;      // Also act as mesh repeater?
  char api_key[64];         // 511.org API key
  uint8_t padding[1];       // Alignment padding
};

// Node roles
#define BAP_ROLE_AUTO     0
#define BAP_ROLE_GATEWAY  1
#define BAP_ROLE_DISPLAY  2

// Agency IDs for 511.org
#define AGENCY_SF_MUNI    1
#define AGENCY_AC_TRANSIT 2
#define AGENCY_BART       3
#define AGENCY_CALTRAIN   4
#define AGENCY_GGT        5
#define AGENCY_SAMTRANS   6
#define AGENCY_VTA        7

// Arrival status codes
#define ARRIVAL_STATUS_ON_TIME   0
#define ARRIVAL_STATUS_DELAYED   1
#define ARRIVAL_STATUS_CANCELLED 2

// Special minutes values
#define ARRIVAL_MINUTES_NA       -1
#define ARRIVAL_MINUTES_DELAYED  -2

// BAP Mesh class - extends Mesh for custom packet handling
class BAPMesh : public mesh::Mesh {
  FILESYSTEM* _fs;
  uint32_t last_millis;
  uint64_t uptime_millis;
  BAPConfig _config;

public:
  BAPMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables);

  void begin(FILESYSTEM* fs);

  // Configuration management
  bool loadConfig();
  bool saveConfig();
  BAPConfig* getConfig() { return &_config; }

  // Determine role based on configuration
  bool isGateway() const { return _config.wifi_ssid[0] != '\0'; }
  bool isDisplay() const { return _config.wifi_ssid[0] == '\0'; }
  bool isRepeater() const { return _config.is_repeater == 1; }

  // Send arrival data over mesh
  void sendArrivals(const BusArrival* arrivals, int count, uint32_t sequence);

  // Callback for received arrival data
  void (*onArrivalsReceived)(const BusArrival* arrivals, int count, uint32_t generated_at) = nullptr;

protected:
  void onRawDataRecv(mesh::Packet* packet) override;
  bool filterRecvFloodPacket(mesh::Packet* packet) override;
  bool allowPacketForward(const mesh::Packet* packet) override;

private:
  uint8_t calcChecksum(const uint8_t* data, size_t len);
};
