#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <RTClib.h>
#include <CayenneLPP.h>
#include <target.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
  using File = fs::File;
#endif

#ifdef WITH_RS232_BRIDGE
#include "helpers/bridges/RS232Bridge.h"
#define WITH_BRIDGE
#endif

#ifdef WITH_ESPNOW_BRIDGE
#include "helpers/bridges/ESPNowBridge.h"
#define WITH_BRIDGE
#endif

#ifdef WITH_MQTT_BRIDGE
#include "helpers/bridges/MQTTBridge.h"
#define WITH_BRIDGE
#include "helpers/esp32/WebConfigServer.h"   // defines WITH_WEBCONFIG on ESP32
#endif

#ifdef WITH_SNMP
#include "helpers/SNMPAgent.h"
#endif

#include <helpers/AdvertDataHelpers.h>
#include <helpers/AlertReporter.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/ClientACL.h>
#include <helpers/CommonCLI.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/StatsFormatHelper.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/RegionMap.h>
#include <helpers/RoutingPolicy.h>
#include "RateLimiter.h"


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

struct NeighbourInfo {
  mesh::Identity id;
  uint32_t advert_timestamp;
  uint32_t heard_timestamp;
  int8_t snr; // multiplied by 4, user should divide to get float value
};

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "14 Aug 2026"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.17.1"
#endif

#define FIRMWARE_ROLE "repeater"

#define PACKET_LOG_FILE  "/packet_log"

class MyMesh : public mesh::Mesh, public CommonCLICallbacks
#ifdef WITH_WEBCONFIG
    , public WebConfigServer::Callbacks
#endif
{
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
  uint8_t reply_path_len;
  TransportKeyStore key_store;
  RegionMap region_map, temp_map;
  RegionEntry* load_stack[8];
  RegionEntry* recv_pkt_region;
  TransportKey default_scope;
  RateLimiter discover_limiter, anon_limiter;
  uint32_t pending_discover_tag;
  unsigned long pending_discover_until;
  bool region_load_active;
  unsigned long dirty_contacts_expiry;
#if MAX_NEIGHBOURS
  NeighbourInfo neighbours[MAX_NEIGHBOURS];
#endif
  CayenneLPP telemetry;
  unsigned long set_radio_at, revert_radio_at;
  unsigned long _ota_update_at = 0;  // deferred `ota update` fire time (0 = none scheduled)
  float pending_freq;
  float pending_bw;
  uint8_t pending_sf;
  uint8_t pending_cr;
  int  matching_peer_indexes[MAX_CLIENTS];
#if defined(WITH_RS232_BRIDGE)
  RS232Bridge bridge;
#elif defined(WITH_ESPNOW_BRIDGE)
  ESPNowBridge bridge;
#elif defined(WITH_MQTT_BRIDGE)
  MQTTBridge* bridge;
#endif
#ifdef WITH_SNMP
  MeshSNMPAgent _snmp_agent;
#endif
#ifdef WITH_MQTT_BRIDGE
  AlertReporter _alerter;
#endif
#ifdef WITH_WEBCONFIG
  WebConfigServer* _webconfig = NULL;  // heap-allocated while running, freed on stop
  bool _wc_batch_active = false;       // coalesce bridge restarts during a config batch
  bool _wc_restart_pending = false;
  uint8_t _wc_slot_restart_mask = 0;
#endif

#if defined(WITH_MQTT_NEIGHBORS)
  // Neighbor-scope discovery: a snapshot of the neighbor table overlaid with an
  // anon-regions query per neighbor, published to the MQTT neighbors topic once
  // every neighbor has responded or timed out.
  enum NeighborDiscoverStatus : uint8_t {
    ND_UNSENT = 0,
    ND_QUEUED = 1,
    ND_PENDING = 2,
    ND_RESPONDED = 3,
    ND_TIMEOUT = 4,
    ND_SEND_FAILED = 5,
  };
  struct NeighborDiscoverEntry {
    mesh::Identity id;       // immutable snapshot: neighbour table can change mid-pass
    uint32_t heard_timestamp;
    int8_t snr;              // multiplied by 4
    uint32_t tag;            // anon-regions request tag we're waiting on
    char scopes[96];         // scope names from the response
    uint8_t status;          // NeighborDiscoverStatus
  };
  NeighborDiscoverEntry neighbor_discover[MAX_NEIGHBOURS];
  uint8_t neighbor_discover_count;
  uint8_t neighbor_discover_next;            // newest-first entry currently being queried
  uint8_t neighbor_discover_publish_count;    // completed prefix that fits the JSON buffer
  uint8_t neighbor_discover_queried_count;    // requests confirmed transmitted
  size_t neighbor_discover_json_size;
  bool neighbor_discover_truncated;
  bool neighbor_discover_active;          // scope-query phase in flight
  bool neighbor_table_refresh_active;     // zero-hop table refresh (stage 1) in flight
  bool neighbor_table_refresh_periodic;   // that refresh was kicked by the periodic timer
  unsigned long neighbor_discover_until;  // current queue or response deadline
  mesh::Packet* neighbor_discover_request; // request awaiting TX completion
  unsigned long next_neighbors_publish;   // periodic publish deadline (0 = fire ASAP)
  char self_scopes_buf[96];
  char self_default_scope_buf[31];
  char neighbor_discover_origin[32];

  mesh::Packet* sendAnonRegionsReq(const mesh::Identity& target, uint32_t& tag);
  bool cancelNeighborDiscoverRequest();
  uint32_t neighborDiscoverQueryTimeoutMs() const;
  bool completeNeighborDiscoverEntry();
  void resetNeighborDiscoverJsonBudget();
  bool neighborDiscoverReady(char* reply);
  bool startNeighborDiscover(char* reply);
  void loopNeighborDiscover();
  void finishNeighborDiscover();
  bool handleNeighborDiscoverResponse(int overlay_idx, const uint8_t* data, size_t len);
  void touchNeighbourHeard(const mesh::Identity& id, uint32_t heard_timestamp);
  void getLocalScopes(char* buf, size_t len);
  // Overlay peer indices are offset by this base so onPeerDataRecv can tell a
  // discovery response apart from a normal ACL-client index.
  static const int NEIGHBOR_DISCOVER_PEER_BASE = 1000;
  static const unsigned long NEIGHBOR_DISCOVER_QUEUE_TIMEOUT_MS = 29000;
  static const int NEIGHBOR_DISCOVER_MIN_FREE_PACKETS = 5;
#endif

  void putNeighbour(const mesh::Identity& id, uint32_t timestamp, float snr);
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

  bool getCADEnabled() const override {
    return _prefs.cad_enabled;
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
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
#ifdef WITH_MQTT_BRIDGE
  uint32_t getRadioWatchdogMillis() const override {
    return ((uint32_t)_cli.getObserverPrefs()->radio_watchdog_minutes) * 60000UL;
  }
#endif
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
  void sendNodeDiscoverReq();
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

#ifdef WITH_MQTT_BRIDGE
  void onAlertConfigChanged() override { _alerter.onConfigChanged(); }
  bool sendAlertText(const char* text) override { return _alerter.sendText(text); }
#endif
  bool resolveAlertScope(TransportKey& dest) override;
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
  void removeNeighbor(const uint8_t* pubkey, int key_len) override;
  void formatStatsReply(char *reply) override;
  void formatRadioStatsReply(char *reply) override;
  void formatRadioDiagReply(char *reply) override;
  void formatPacketStatsReply(char *reply) override;
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
    if (!bridge) {
#ifdef WITH_MQTT_BRIDGE
      bridge = new MQTTBridge(&_prefs, _cli.getObserverPrefs(), _mgr, getRTCClock(), &self_id);
#endif
      if (!bridge) return;
    }
    if (enable == bridge->isRunning()) return;
    if (enable)
    {
      // Set device metadata before starting bridge (same as in begin())
      char device_id[65];
      mesh::LocalIdentity self_id = getSelfId();
      mesh::Utils::toHex(device_id, self_id.pub_key, PUB_KEY_SIZE);
      bridge->setDeviceID(device_id);
      bridge->setFirmwareVersion(getFirmwareVer());
      bridge->setBoardModel(_cli.getBoard()->getManufacturerName());
      bridge->setBuildDate(getBuildDate());
#ifdef WITH_MQTT_BRIDGE
      bridge->setStatsSources(this, _radio, _cli.getBoard(), _ms);
#endif
      bridge->begin();
#ifdef WITH_MQTT_BRIDGE
      _alerter.setBridge(bridge);
#endif
    }
    else
    {
      bridge->end();
#ifdef WITH_MQTT_BRIDGE
      _alerter.setBridge(nullptr);
#endif
    }
  }

  void restartBridge() override {
    if (!bridge || !bridge->isRunning()) return;
#ifdef WITH_WEBCONFIG
    if (_wc_batch_active) {   // coalesced: applied once in onConfigBatchEnd()
      _wc_restart_pending = true;
      return;
    }
#endif
    bridge->end();
    // Set device metadata before restarting bridge (same as in begin())
    char device_id[65];
    mesh::LocalIdentity self_id = getSelfId();
    mesh::Utils::toHex(device_id, self_id.pub_key, PUB_KEY_SIZE);
    bridge->setDeviceID(device_id);
    bridge->setFirmwareVersion(getFirmwareVer());
    bridge->setBoardModel(_cli.getBoard()->getManufacturerName());
    bridge->setBuildDate(getBuildDate());
#ifdef WITH_MQTT_BRIDGE
    bridge->setStatsSources(this, _radio, _cli.getBoard(), _ms);
#endif
    bridge->begin();
  }

  void restartBridgeSlot(int slot) override {
#ifdef WITH_MQTT_BRIDGE
    if (!bridge || !bridge->isRunning()) return;
#ifdef WITH_WEBCONFIG
    if (_wc_batch_active && slot >= 0 && slot < 8) {
      _wc_slot_restart_mask |= (uint8_t)(1u << slot);
      return;
    }
#endif
    bridge->setSlotPreset(slot, _cli.getObserverPrefs()->mqtt_slot_preset[slot]);
#else
    (void)slot;
#endif
  }

#if defined(WITH_MQTT_BRIDGE)
  // Broadcast a key OTA milestone (start/fail only) on the configured alert
  // channel, in addition to the Serial log — so an operator who triggered
  // `ota update` via remote management still gets feedback that lands well after
  // the command's reply window. Respects the `alert on/off` master switch and
  // rides the configured alert scope (sendChannel -> resolveAlertScope); a no-op
  // when alerts are off or no channel is set. Deliberately NOT wired to routine
  // slot connect/disconnect — those remain in AlertReporter's fault logic.
  void otaAlert(const char* msg) {
    auto* obs = _cli.getObserverPrefs();
    if (obs && obs->alert_enabled) _alerter.sendText(msg);
  }

  // Best-effort flush of the outbound packet queue before an OTA teardown that
  // blocks the loop until reboot. The START alert (otaAlert) and the CLI reply
  // are queued fire-and-forget (delay 0 / CLI_REPLY_DELAY_MILLIS); once
  // setBridgeState(false) + otaFromManifest() run they spin the loop task until
  // the chip reboots, so anything still in the send queue at that point is
  // silently lost — the observed "OTA update starting never arrives" case on a
  // busy / duty-limited channel where the packet can't win a TX slot inside the
  // 2.5 s window. Pump the mesh loop so already-queued packets get their airtime,
  // bounded by timeout_ms so a jammed or budget-exhausted channel can't stall the
  // update. Respects duty cycle / CAD: it only drains what is queued, it does not
  // force a transmit. Returns instantly on a healthy node (queue already empty).
  void drainOutbound(uint32_t timeout_ms) {
    unsigned long start = millis();
    while (hasOutbound() || _mgr->getOutboundCount(millis()) > 0) {
      if (millis() - start >= timeout_ms) break;
      mesh::Mesh::loop();  // base dispatcher only — drives RX + checkSend()/TX
      delay(1);            // yield to the radio ISR / other FreeRTOS tasks
    }
  }
#endif

  // Schedule the pull-OTA flash to run from loop() in ~2.5 s, leaving time for the
  // "Beginning update..." CLI reply (CLI_REPLY_DELAY_MILLIS = 600 ms) to transmit
  // before the flash blocks the loop and reboots.
  bool beginDeferredOtaUpdate() override {
    _ota_update_at = millis() + 2500;
    if (_ota_update_at == 0) _ota_update_at = 1;  // 0 means "none"
#if defined(WITH_MQTT_BRIDGE)
    // Broadcast START now, while the loop still runs (the 2.5 s reply window):
    // the deferred flash blocks the loop and, on success, reboots — so a start
    // alert queued at fire time could never transmit. See otaAlert().
    otaAlert("OTA update starting");
#endif
    return true;
  }

  int getQueueSize() override {
    return bridge ? bridge->getQueueSize() : 0;
  }

  bool isMqttBridgeRunning() override {
    return bridge && bridge->isRunning();
  }

  bool syncMqttNtp() override {
    if (!bridge || !bridge->isRunning()) return false;
    // Queue the sync onto the MQTT task (Core 0) without blocking: this runs on
    // the Arduino loop task (serial CLI and the web config batch both drain
    // here), and blocking up to 30 s would stall mesh/radio forwarding. Returns
    // true once queued; verify with `get mqtt.ntp.diag`.
    return bridge->requestForcedNtpSync(0);
  }

  bool runMqttNtpDiag(char* reply, size_t reply_size, bool verbose) override {
    if (!bridge || !bridge->isRunning()) return false;
    return bridge->ntpDiag(reply, reply_size, verbose);
  }
#endif

#ifdef WITH_WEBCONFIG
  // CommonCLICallbacks: `start webconfig [ap]` / `stop webconfig`
  bool startWebConfig(bool force_ap, char* reply) override;
  bool stopWebConfig(char* reply) override;

  // WebConfigServer::Callbacks - all invoked from tick() on the loop task
  void execCommand(char* cmd, char* reply) override {
    handleCommand(0, cmd, reply);
  }
  void rebootNow() override {
    _cli.getBoard()->reboot();
  }
  void onConfigBatchStart() override {
    _wc_batch_active = true;
    _wc_restart_pending = false;
    _wc_slot_restart_mask = 0;
  }
  void onConfigBatchEnd() override;
  void buildStatsJson(char* buf, size_t buf_size) override;
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

  bool setRxBoostedGain(bool enable) override;

  #if defined(USE_LR2021)
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) override;
  #endif

};
