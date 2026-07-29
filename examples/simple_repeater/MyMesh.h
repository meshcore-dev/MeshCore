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

#ifdef WITH_RS232_BRIDGE
#include "helpers/bridges/RS232Bridge.h"
#define WITH_BRIDGE
#endif

#ifdef WITH_ESPNOW_BRIDGE
#include "helpers/bridges/ESPNowBridge.h"
#define WITH_BRIDGE
#endif

#include <helpers/AdvertDataHelpers.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/ClientACL.h>
#include <helpers/CommonCLI.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/FloodSuppression.h>
#include <helpers/NeighbourLinkTable.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/StatsFormatHelper.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/RegionMap.h>
#include "RateLimiter.h"

#ifdef WITH_BRIDGE
extern AbstractBridge* bridge;
#endif

struct RepeaterStats {
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  int16_t  noise_floor;
  int16_t  last_rssi;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint16_t err_events;                // was 'n_full_events'
  int16_t  last_snr;   // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint32_t total_rx_air_time_secs;
  uint32_t n_recv_errors;
};

#ifndef MAX_CLIENTS
  #define MAX_CLIENTS           32
#endif

#ifndef MAX_ATTACHED_CLIENTS
  #define MAX_ATTACHED_CLIENTS  16
#endif
#ifndef ATTACHED_CLIENT_FRESH_S
  #define ATTACHED_CLIENT_FRESH_S  (24UL * 3600UL)   // ~24h -- attached leaf clients are stable
#endif

struct NeighbourInfo {
  mesh::Identity id;
  uint32_t advert_timestamp;
  uint32_t heard_timestamp;
  int8_t snr; // multiplied by 4, user should divide to get float value
};

// A leaf CLIENT (companion/sensor/room-server) directly attached to this repeater
// (M is its first hop). Tracked so suppression does not starve attached clients of
// floods they need. Match key is the 1-byte path hash (prefix[0]); `prefix` carries
// up to 4 identity bytes for display (4 from an advert, 1 from a message src_hash).
struct AttachedClient {
  uint8_t  prefix[4];   // identity prefix learned (match key = prefix[0])
  uint8_t  prefix_len;  // bytes actually known: 4 (advert) or 1 (msg src_hash)
  uint32_t last_seen;   // RTC seconds
  bool     active;
};

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "6 Jun 2026"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.16.0"
#endif

#define FIRMWARE_ROLE "repeater"

#define PACKET_LOG_FILE  "/packet_log"

class MyMesh : public mesh::Mesh, public CommonCLICallbacks {
  FILESYSTEM* _fs;
  uint32_t last_millis;
  uint64_t uptime_millis;
  unsigned long next_local_advert, next_flood_advert;
  bool _logging;
  NodePrefs _prefs;
  ClientACL  acl;
  CommonCLI _cli;
  uint8_t reply_data[MAX_PACKET_PAYLOAD];
  uint8_t reply_path[MAX_PATH_SIZE];
  int8_t  reply_path_len;
  uint8_t reply_path_hash_size;
  TransportKeyStore key_store;
  RegionMap region_map, temp_map;
  RegionEntry* load_stack[8];
  RegionEntry* recv_pkt_region;
  TransportKey default_scope;
  RateLimiter discover_limiter, anon_limiter;
  FloodSuppressionTable _flood_supp;   // redundancy-aware FLOOD suppression state
  NeighbourLinkTable _nbr_links;       // inter-near-neighbour reach edges (coverage inference)
  AttachedClient _attached[MAX_ATTACHED_CLIENTS] = {};  // directly-attached leaf clients (client-aware suppression)
  // Near-neighbour freshness window for the coverage test and the adaptive-density
  // count, 60 min
  static const uint32_t NEIGHBOUR_FRESH_S = 3600;
  // Adaptive (neighbour-derived) effective params, recomputed in loop() under #if MAX_NEIGHBOURS.
  uint8_t  _fs_eff_c;            // derived threshold C (0 = off); used when _fs_adaptive_active
  int8_t   _fs_eff_hi;           // derived snr_hi (dB); used when _fs_adaptive_active
  bool     _fs_adaptive_active;  // neighbour data available this cycle? (else static fallback)
  uint8_t  _fs_pending_c;        // debounce: candidate c awaiting a 2nd confirming cycle
  uint32_t _fs_next_recompute_ms;
  uint32_t _fs_seen;        // distinct floods heard (denominator of suppression ratio)
  uint32_t _fs_suppressed;  // floods whose rebroadcast was made redundant (numerator)
  uint32_t pending_discover_tag;
  unsigned long pending_discover_until;
  bool region_load_active;
  unsigned long dirty_contacts_expiry;
#if MAX_NEIGHBOURS
  NeighbourInfo neighbours[MAX_NEIGHBOURS];
#endif
  CayenneLPP telemetry;
  unsigned long set_radio_at, revert_radio_at;
  float pending_freq;
  float pending_bw;
  uint8_t pending_sf;
  uint8_t pending_cr;
  int  matching_peer_indexes[MAX_CLIENTS];
#if defined(WITH_RS232_BRIDGE)
  RS232Bridge bridge;
#elif defined(WITH_ESPNOW_BRIDGE)
  ESPNowBridge bridge;
#endif

  void putNeighbour(const mesh::Identity& id, uint32_t timestamp, float snr);
  void touchNeighbourByHash(const mesh::Packet* packet);  // refresh a KNOWN neighbour's liveness/SNR from an overheard forward
  bool isNearNeighbour(int i, uint32_t now) const;        // fresh (<=NEIGHBOUR_FRESH_S) and SNR>=snr_lo
  int8_t findNearNeighbour(const uint8_t* h, uint8_t hs, uint32_t now) const;  // index of near neighbour matching hash, else -1
  bool allNearNeighboursCovered(const FloodSuppressionEntry& e, uint32_t now) const;  // >=1 near && every near neighbour in e.covered
  bool nearReaches(int from_i, int to_j, uint8_t hs, uint32_t now) const;  // fresh DIRECTED reach edge: neighbours[from_i] reaches neighbours[to_j] (to_j heard from_i)
  bool clientProtectionAllowsSuppress(const mesh::Packet* pkt, uint32_t now) const;  // 3-tier client-aware gate (always active)
  void addOrRefreshAttachedClient(const uint8_t* prefix, uint8_t plen, uint32_t now);  // seed/refresh attached leaf client (prefix[0] is the match key)
  bool attachedClientMatches(uint8_t hash1, uint32_t now) const; // is hash1 a fresh attached client? (hash1 vs prefix[0])
  void removeAttachedClient(uint8_t hash1);                      // reconcile: node turned out to be a repeater (hash1 vs prefix[0])
  void purgeAttachedClients(uint32_t now);                        // evict stale clients (~24h)
  bool isKnownRepeaterHash1(uint8_t hash1) const;                 // does this 1-byte hash match a known repeater neighbour?
  void cancelPendingFloodOutbound(const uint8_t* hash);   // cancel our scheduled flood rebroadcast (if any)
  void updateAdaptiveFloodParams();                       // derive _fs_eff_c/_fs_eff_hi from neighbour table
  uint8_t effectiveFloodSuppressC() const;                // adaptive? _fs_eff_c : flood_suppress_c
  int8_t effectiveFloodSuppressSnrHi() const;             // adaptive? _fs_eff_hi : flood_suppress_snr_hi
  uint8_t handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood);
  uint8_t handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  int handleRequest(ClientInfo* sender, uint32_t sender_timestamp, uint8_t* payload, size_t payload_len);
  mesh::Packet* createSelfAdvert();

  File openAppend(const char* fname);
  bool isLooped(const mesh::Packet* packet, const uint8_t max_counters[]);

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  bool allowPacketForward(const mesh::Packet* packet) override;
  const char* getLogDateTime() override;
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;

  void logRx(mesh::Packet* pkt, int len, float score) override;
  void logTx(mesh::Packet* pkt, int len) override;
  void logTxFail(mesh::Packet* pkt, int len) override;
  int calcRxDelay(float score, uint32_t air_time) const override;

  uint32_t getRetransmitDelay(const mesh::Packet* packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet* packet) override;

  int getInterferenceThreshold() const override {
    return _prefs.interference_threshold;
  }
  bool getCADEnabled() const override {
    return _prefs.cad_enabled;
  }
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
  uint8_t getExtraAckTransmitCount() const override {
    return _prefs.multi_acks;
  }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
    sensors.setSettingValue("gps", _prefs.gps_enabled?"1":"0");
  }
#endif

  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;

  void onAnonDataRecv(mesh::Packet* packet, const uint8_t* secret, const mesh::Identity& sender, uint8_t* data, size_t len) override;
  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len);
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override;
  bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onControlDataRecv(mesh::Packet* packet) override;

  void sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size);

public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables);

  void begin(FILESYSTEM* fs);
  void sendNodeDiscoverReq(uint32_t delay_millis = 0);
  const char* getFirmwareVer() override { return FIRMWARE_VERSION; }
  const char* getBuildDate() override { return FIRMWARE_BUILD_DATE; }
  const char* getRole() override { return FIRMWARE_ROLE; }
  const char* getNodeName() { return _prefs.node_name; }
  NodePrefs* getNodePrefs() {
    return &_prefs;
  }

  void savePrefs() override {
    _cli.savePrefs(_fs);
  }

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size);

  // CommonCLICallbacks
  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) override;
  bool formatFileSystem() override;
  void sendSelfAdvertisement(int delay_millis, bool flood) override;
  void updateAdvertTimer() override;
  void updateFloodAdvertTimer() override;

  void setLoggingOn(bool enable) override { _logging = enable; }

  void eraseLogFile() override {
    _fs->remove(PACKET_LOG_FILE);
  }

  void dumpLogFile() override;
  void setTxPower(int8_t power_dbm) override;
  void formatNeighborsReply(char *reply) override;
  void formatClientsReply(char *reply) override;                 // list attached leaf clients
  void formatReachReply(char *reply, const uint8_t* hash, uint8_t hash_len) override;  // reach edges of a near repeater
  void removeNeighbor(const uint8_t* pubkey, int key_len) override;
  void formatStatsReply(char *reply) override;
  void formatRadioStatsReply(char *reply) override;
  void formatPacketStatsReply(char *reply) override;
  void formatFloodSuppressRatioReply(char *reply) override;
  void startRegionsLoad() override;
  bool saveRegions() override;
  void onDefaultRegionChanged(const RegionEntry* r) override;

  mesh::LocalIdentity& getSelfId() override { return self_id; }

  void saveIdentity(const mesh::LocalIdentity& new_id) override;
  void clearStats() override;

  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  void loop();

#if defined(WITH_BRIDGE)
  void setBridgeState(bool enable) override {
    if (enable == bridge.isRunning()) return;
    if (enable)
    {
      bridge.begin();
    }
    else 
    {
      bridge.end();
    }
  }

  void restartBridge() override {
    if (!bridge.isRunning()) return;
    bridge.end();
    bridge.begin();
  }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

  bool setRxBoostedGain(bool enable) override;

};
