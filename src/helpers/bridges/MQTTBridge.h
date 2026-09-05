#pragma once

#include "MeshCore.h"
#include "helpers/bridges/BridgeBase.h"
#include <PsychicMqttClient.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include "helpers/JWTHelper.h"
#include "helpers/MQTTPacketFilter.h"
#include "helpers/MQTTPresets.h"
#include "helpers/MQTTLifecycle.h"
#include <atomic>

#ifdef WITH_SNMP
class MeshSNMPAgent;  // Forward declaration
#endif

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#endif

#if defined(MQTT_DEBUG) && defined(ARDUINO)
  #include <Arduino.h>
  // USB CDC-aware debug macros: only print if Serial is ready (non-blocking check)
  // Serial.availableForWrite() returns bytes available in write buffer (>0 means ready)
  // This prevents hangs when USB CDC isn't ready yet (e.g., ESP32-S3 native USB)
  #define MQTT_DEBUG_PRINT(F, ...) do { if (Serial.availableForWrite() > 0) { Serial.printf("MQTT: " F, ##__VA_ARGS__); } } while(0)
  #define MQTT_DEBUG_PRINTLN(F, ...) do { if (Serial.availableForWrite() > 0) { Serial.printf("MQTT: " F "\n", ##__VA_ARGS__); } } while(0)
#else
  #define MQTT_DEBUG_PRINT(...) {}
  #define MQTT_DEBUG_PRINTLN(...) {}
#endif

#ifdef WITH_MQTT_BRIDGE

// Periodic neighbors publication keys off the mesh neighbor cache (sized by
// MAX_NEIGHBOURS) and needs a persistent JSON buffer plus a second transient one
// while the mesh builds the table. PSRAM boards get it automatically; non-PSRAM
// boards must opt in per variant with MQTT_NEIGHBORS_WITHOUT_PSRAM, which spends
// internal DRAM the TLS stack also needs (see NEIGHBORS_JSON_BUFFER_SIZE). Every
// neighbors-specific member, method, and code block here is gated on
// WITH_MQTT_NEIGHBORS.
#if defined(MAX_NEIGHBOURS) && MAX_NEIGHBOURS > 0 && \
    (defined(BOARD_HAS_PSRAM) || defined(MQTT_NEIGHBORS_WITHOUT_PSRAM))
#define WITH_MQTT_NEIGHBORS 1
#endif

/**
 * @brief Bridge implementation using MQTT protocol for packet transport
 *
 * This bridge enables mesh packet transport over MQTT, allowing repeaters to
 * uplink packet data to multiple MQTT brokers for monitoring and analysis.
 *
 * Features:
 * - Up to 6 configurable MQTT connection slots (5 active with PSRAM, 2 without)
 * - Built-in presets for LetsMesh Analyzer (US/EU), MeshMapper, MeshRank, Waev, CascadiaMesh
 * - Custom broker support with username/password auth
 * - JWT authentication with Ed25519 device signing
 * - Automatic reconnection with exponential backoff
 * - JSON message formatting for status, packets, and raw data
 * - Packet queuing during connection issues
 *
 * Configuration:
 * - Define WITH_MQTT_BRIDGE to enable this bridge
 * - Configure slots via: set mqtt1.preset <name>, set mqtt2.preset <name>, etc.
 * - Available presets: analyzer-us, analyzer-eu, meshmapper, custom, none
 */
class MQTTBridge : public BridgeBase {
public:
  // Max NTP servers in a try-list: 1 custom primary + the built-in fallbacks.
  static const int kMaxNtpServers = 6;

private:
  static const size_t AUTH_TOKEN_SIZE = 768;

  // Connection slot - each slot holds one MQTT connection
  struct MQTTSlot {
    PsychicMqttClient* client;
    const MQTTPresetDef* preset;    // Points to MQTT_PRESETS[] entry, nullptr for custom/none
    bool enabled;                   // true when preset is not "none"
    bool connected;                 // Updated in callbacks
    bool initial_connect_done;      // True after first connect() call

    // JWT auth state (used by preset JWT slots and custom slots with audience set).
    // nullptr until this slot first creates a token, so a slot that is unconfigured,
    // capped off, or on a non-JWT preset never pays for AUTH_TOKEN_SIZE; slots that do
    // use JWT keep their buffer in PSRAM where the board has it. Allocated by
    // ensureSlotAuthToken() and then held for the client's lifetime -- never freed per
    // reconnect (alloc/free churn is a fragmentation source) and never freed on
    // teardown, because setCredentials() hands this exact pointer to the client and
    // esp-mqtt re-reads it whenever a later connect() re-applies a dirtied config.
    // Freed only alongside the client in destroySlotClients().
    char* auth_token;               // nullptr or empty string = no valid token
    unsigned long token_expires_at;
    unsigned long last_token_renewal;

    // Custom broker settings (only used when preset_name is "custom")
    char host[64];
    uint16_t port;
    char username[32];
    char password[64];
    char audience[64];              // JWT audience for custom JWT slots (empty = username/password)
    char broker_uri[128];           // Persistent URI for custom slots (avoids dangling pointer)

    // Reconnect backoff
    uint8_t reconnect_backoff;      // 0..4 index into backoff table
    uint8_t max_backoff_failures;   // consecutive failures at max backoff level
    bool circuit_breaker_tripped;   // true = stop reconnecting until reconfigured
    unsigned long connected_at_ms;  // millis() of last successful connect (0 = not connected);
                                    // gates the stability-based backoff reset in maintenance
    unsigned long last_reconnect_attempt;
    unsigned long last_log_time;    // Throttle disconnect log messages
    unsigned long last_deferred_log_ms; // Throttle "connect deferred" log spam (Phase 1)

    // Last error (stored for CLI diagnostics — serial-free debugging)
    int32_t last_tls_err;           // esp_tls_last_esp_err (0 = no error)
    int32_t last_tls_stack_err;     // mbedTLS stack error
    int last_sock_errno;            // socket errno
    unsigned long last_error_time;  // millis() of last error
    uint32_t disconnect_count;      // Number of disconnect callbacks since boot
    unsigned long first_disconnect_time; // millis() of first disconnect after boot

    // Current-outage timer (used by AlertReporter to fire faults after a sustained
    // outage). Reset to 0 on each successful connect, set to millis() on first
    // disconnect-after-connect. first_disconnect_time is intentionally separate
    // so the existing 'mqttN.diag' "first_disc" semantics don't change.
    unsigned long current_outage_started_ms;
  };

  MQTTSlot _slots[RUNTIME_MQTT_SLOTS];

  // JWT username shared across all JWT-auth slots (same device identity)
  char _jwt_username[70];  // Format: v1_{UPPERCASE_PUBLIC_KEY}

  // Message configuration
  char _origin[32];
  char _iata[8];
  char _device_id[65];  // Device public key (hex string)
  char _firmware_version[64];  // Firmware version string
  char _board_model[64];  // Board model string
  char _build_date[32];  // Build date string
  bool _status_enabled;
  bool _packets_enabled;
  bool _raw_enabled;
  bool _rx_enabled;
  uint8_t _tx_mode;  // 0=off, 1=all TX, 2=self-advert only
  unsigned long _last_status_publish;
  unsigned long _status_interval;

  // Packet queue for offline scenarios
  // NOTE: We store a full copy of the packet (not a pointer) because the
  // Dispatcher frees packets back to the static pool immediately after logRx()
  // returns. Storing only a pointer would be a use-after-free.
  struct QueuedPacket {
    mesh::Packet packet_copy;  // ~258 bytes, full value copy
    unsigned long timestamp;
    unsigned long next_retry_ms;  // Earliest millis() when a failed send may be retried
    uint8_t retry_attempts;       // Bounded resend attempts for transient QoS0 publish failures
    bool is_tx;
    float snr;
    float rssi;
    // Raw radio bytes embedded at enqueue time (Core 1), never shared across cores.
    // On non-PSRAM boards the queue is smaller (6 items) to offset the per-item cost.
    uint8_t raw_data[256];
    uint8_t raw_len;
    bool has_raw_data;
  };

  #if defined(BOARD_HAS_PSRAM)
  static const int MAX_QUEUE_SIZE = 50;
  #else
  // Reduced from 10: raw_data[256] adds ~256 bytes/item; 6×543 ≈ 3.3 KB vs old 10×282 ≈ 2.8 KB.
  // Net increase is <500 bytes; _last_raw_data (256 bytes) is eliminated to offset further.
  static const int MAX_QUEUE_SIZE = 6;
  #endif

  // FreeRTOS queue for thread-safe packet queuing
  #ifdef ESP_PLATFORM
  QueueHandle_t _packet_queue_handle;
  TaskHandle_t _mqtt_task_handle;
  // Packet queue storage: PSRAM heap on PSRAM boards, inline array on non-PSRAM boards.
  // Using xQueueCreateStatic with inline storage eliminates a separate heap allocation.
  uint8_t* _packet_queue_storage;
  StaticQueue_t _packet_queue_struct;
  #if !defined(BOARD_HAS_PSRAM)
  uint8_t _packet_queue_inline[MAX_QUEUE_SIZE * sizeof(QueuedPacket)];
  #endif
  #else
  // Fallback to circular buffer for non-ESP32 platforms
  QueuedPacket _packet_queue[MAX_QUEUE_SIZE];
  int _queue_head;
  int _queue_tail;
  #endif
  int _queue_count;  // Protected by queue operations or mutex

  // NTP time sync
  WiFiUDP _ntp_udp;
  NTPClient _ntp_client;
  unsigned long _last_ntp_sync;
  bool _ntp_synced;
  bool _ntp_sync_pending;  // Flag to trigger NTP sync from loop() instead of event handler
  bool _slots_setup_done;  // Deferred: slots set up after NTP sync
  // WiFi.onEvent() handler registered once and never removed by end(); the bridge
  // object is reused across restarts, so re-registering would leak handlers and
  // duplicate every connect/disconnect log line. Inline-initialised so it survives
  // construction and is NOT reset by end().
  bool _wifi_event_registered = false;
  int _max_active_slots;   // Runtime limit: 5 with PSRAM, 2 without

  // Pending slot reconfigure: set from CLI (Core 1), processed by MQTT task (Core 0)
  volatile bool _slot_reconfigure_pending[RUNTIME_MQTT_SLOTS];

  // Pending on-connect status publish: set from the onConnect callback (which
  // runs on the esp-mqtt event task, NOT this bridge task), consumed by the MQTT
  // task (Core 0). publishStatusToSlot() touches the shared status doc/buffer/
  // origin that publishStatus() also uses, so it must run only on the bridge
  // task — the callback just raises this flag. Same idiom as
  // _slot_reconfigure_pending; a single-byte volatile store/load is atomic.
  volatile bool _status_publish_pending[RUNTIME_MQTT_SLOTS];

  // CLI-requested forced NTP sync, marshalled onto the MQTT task (Core 0).
  // All NTP I/O (_ntp_client, configTime) must run on Core 0; the CLI thread
  // (Core 1) sets _ntp_force_requested and blocks in requestForcedNtpSync()
  // until the task publishes the outcome via _ntp_force_result/_ntp_force_done.
  // Single-requester assumption: CLI commands are serialized, so at most one
  // forced sync is outstanding at a time.
  volatile bool _ntp_force_requested;
  volatile bool _ntp_force_done;
  volatile bool _ntp_force_result;

  // CLI-requested NTP connectivity diagnostic, marshalled onto the MQTT task (Core 0)
  // with the same handshake as the forced sync. Probe-only: it queries each server and
  // records the reported time but never calls configTime()/setCurrentTime(), so the
  // system clock is left untouched. Results are written by the task and read by the CLI
  // thread once _ntp_diag_done is set.
  volatile bool _ntp_diag_requested;
  volatile bool _ntp_diag_done;
  struct NtpDiagResult {
    char     server[64];
    bool     ok;
    uint32_t epoch;  // server-reported UTC epoch when ok
  };
  NtpDiagResult _ntp_diag_results[kMaxNtpServers];
  int _ntp_diag_count;

  // Cooperative-shutdown handshake (Phase 5). The loop task (Core 1) raises
  // _stop_requested through the lifecycle Coordinator; the MQTT task (Core 0)
  // sees it, tears down its own clients on Core 0 (where the mbedTLS contexts
  // live), sets _stop_acked LAST, and self-terminates. end() waits for the ack
  // before freeing the queue/buffers. Plain volatile matches the existing
  // NTP/reconfigure handshake idiom above; replacing all of these with a command
  // channel / task notifications is explicitly deferred (see MQTT_OWNERSHIP.md).
  volatile bool _stop_requested = false;
  volatile bool _stop_acked = false;

  // Timezone handling.
  // _timezone_storage is inline class storage (zero heap) that is reconfigured
  // via setRules() whenever the preferred timezone string changes. _timezone
  // is a stable alias pointer to &_timezone_storage so existing call sites
  // that accept a Timezone* keep working without modification.
  Timezone _timezone_storage;
  Timezone* _timezone;

  // Core 1-only staging: written by storeRawRadioData(), consumed by queuePacket().
  // No mutex needed — both call sites run on Core 1 in guaranteed sequence.
  static const size_t LAST_RAW_DATA_SIZE = 256;
  uint8_t _staged_raw[LAST_RAW_DATA_SIZE];
  int     _staged_raw_len   = 0;
  float   _staged_snr       = 0.0f;
  float   _staged_rssi      = 0.0f;
  bool    _staged_raw_valid = false;

  // Core 0-owned copy of the most recent raw data — written only by processPacketQueue()
  // on Core 0, read only by publishStatus() on Core 0. No mutex required.
  // On PSRAM boards: heap pointer into PSRAM. On non-PSRAM: inline array in class object.
  static const size_t LAST_RAW_DATA_SIZE_MEMBER = 256;  // mirrors LAST_RAW_DATA_SIZE
  #if defined(BOARD_HAS_PSRAM)
  uint8_t* _last_raw_data;
  #else
  uint8_t  _last_raw_data[LAST_RAW_DATA_SIZE_MEMBER];
  #endif
  int _last_raw_len;
  float _last_snr;
  float _last_rssi;
  unsigned long _last_raw_timestamp;

  // One JSON serialization buffer shared by every publish path — packet, raw, and
  // status all serialize on the bridge task (Core 0), so they are never in flight at
  // the same time and a second buffer bought nothing. Reused rather than reallocated
  // per publish (no alloc/free churn). On PSRAM boards: heap pointer into PSRAM to save
  // internal heap. On non-PSRAM: inline in the class object so the allocation doesn't
  // interleave with large TLS buffers at startup.
  static const size_t PUBLISH_JSON_BUFFER_SIZE = 2048;
  // Status keeps its own smaller ceiling: raising it would change which oversized
  // status documents get published instead of dropped.
  static const size_t STATUS_JSON_BUFFER_SIZE = 768;
  static_assert(STATUS_JSON_BUFFER_SIZE <= PUBLISH_JSON_BUFFER_SIZE,
                "status payloads serialize into the shared publish buffer");
  #if defined(BOARD_HAS_PSRAM)
  char* _json_scratch_buffer;
  #else
  char _json_scratch_buffer[PUBLISH_JSON_BUFFER_SIZE];
  #endif

#if defined(WITH_MQTT_NEIGHBORS)
  // Persistent PSRAM copy of the neighbors-table JSON. The mesh (Core 1) builds
  // the payload into its own transient buffer, hands it here via
  // requestPublishNeighbors(), and the MQTT task (Core 0) publishes this copy.
  // Allocated in allocateRuntimeBuffers()/freed in releaseRuntimeBuffers() like
  // the other PSRAM buffers (nullptr if the allocation failed).
  char* _neighbors_json_buffer;
  size_t _neighbors_publish_len;
  // Release/acquire handoff from the mesh loop (Core 1) to the MQTT task (Core 0).
  // A second snapshot is dropped while the current one is still publishing.
  std::atomic<bool> _neighbors_publish_pending;
  // Written by the MQTT task (Core 0), read by the CLI (Core 1) for `get mqtt.status`.
  enum NeighborsResult : uint8_t { NBR_RESULT_NONE, NBR_RESULT_OK, NBR_RESULT_FAIL };
  std::atomic<uint8_t> _neighbors_last_result;
  // Written by the mesh loop (Core 1), read by the CLI (Core 1). Cached schedule
  // summary so the wrap-safe millis math stays on the mesh side that owns the timer.
  std::atomic<uint8_t> _neighbors_phase;
  std::atomic<uint32_t> _neighbors_secs_until_next;
#endif

  // Routes the shared document's pools to PSRAM where the board has it, matching the
  // neighbors document's allocator in MyMesh.cpp. ArduinoJson's default allocator is
  // plain malloc(), which puts every per-publish pool block in internal DRAM next to
  // the mbedTLS working set. A block is ARDUINOJSON_POOL_CAPACITY slots: these targets
  // are 32-bit, so ARDUINOJSON_SLOT_ID_SIZE is 2 and that resolves to 128 slots =
  // 1024 bytes per block, not the 4096 quoted near NEIGHBORS_DOC_POOL_BUDGET below
  // (which describes a 64-bit configuration; its own byte measurements still stand).
  struct JsonScratchAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override;
    void deallocate(void* ptr) override;
    void* reallocate(void* ptr, size_t new_size) override;
  };
  JsonScratchAllocator _json_allocator;

  // Shared by the packet/raw/status builders, like _json_scratch_buffer above.
  // Declared after _json_allocator so the allocator is constructed first.
  // This was a StaticJsonDocument<N> described as an inline pool; under ArduinoJson 7
  // that is a deprecated empty subclass of JsonDocument whose template argument only
  // feeds capacity(), so the object is 64 bytes and every pool comes from the allocator.
  JsonDocument _json_scratch_doc{&_json_allocator};

  // Memory pressure monitoring (per-publish skip; see publishPacket()).
  // The broader fragmentation-recovery machinery was removed in Phase 4 of
  // the MQTT memory-defrag work — persistent MQTT clients no longer churn
  // the heap, so gray-zone / critical-restart trackers are unnecessary.
  unsigned long _last_memory_check;
  bool _memory_pressure = false;  // Cached max-alloc verdict; re-sampled at most once per interval in publishPacket() so the heap walk isn't paid per-packet under pressure
  int _skipped_publishes;  // Exposed via SNMP; count of publishes skipped when max_alloc is too low
  // Packets rejected by the per-slot filters before reaching the queue. Written
  // on Core 1 (radio callbacks), read on Core 0 for `mqtt.stats`; a torn read of
  // a diagnostic counter is harmless, so no atomic is warranted.
  unsigned long _filtered_packets = 0;

  // Status publish retry tracking
  unsigned long _last_status_retry;  // Track last retry attempt (separate from successful publish)
  static const unsigned long STATUS_RETRY_INTERVAL = 30000; // Retry every 30 seconds if failed

  // Device identity for JWT token creation
  mesh::LocalIdentity *_identity;

  // Cached connection status (updated in callbacks to avoid redundant checks)
  bool _cached_has_connected_slots;

  // Queue staleness tracking
  unsigned long _queue_disconnected_since;  // 0 = has connected slots

#ifdef WITH_SNMP
  MeshSNMPAgent* _snmp_agent;
#endif

  // Throttle logging
  unsigned long _last_no_broker_log;
  static const unsigned long NO_BROKER_LOG_INTERVAL = 30000; // Log every 30 seconds max
  static const unsigned long SLOT_LOG_INTERVAL = 30000; // Log every 30 seconds max
  // Retry cadence for a slot whose setup failed on an allocation. Deliberately slower
  // than the first backoff rung: the failure means internal heap is exhausted, and a
  // retry that succeeds immediately launches a TLS handshake.
  static const unsigned long SLOT_SETUP_RETRY_INTERVAL = 60000;
  unsigned long _last_config_warning; // Throttle configuration mismatch warnings
  static const unsigned long CONFIG_WARNING_INTERVAL = 300000; // Log every 5 minutes max

  // WiFi connection state and exponential backoff
  unsigned long _last_wifi_check;
  wl_status_t _last_wifi_status;
  bool _wifi_status_initialized;
  unsigned long _wifi_disconnected_time;  // 0 when connected
  unsigned long _last_wifi_reconnect_attempt;
  uint8_t _wifi_reconnect_backoff_attempt;  // 0..5 → 15s, 30s, 60s, 120s, 300s; reset on connect
  unsigned long _last_slot_reconnect_ms;   // guards against concurrent TLS handshakes (15 s inter-slot gap)

  // Optional pointers for collecting stats internally (set by mesh if available)
  mesh::Dispatcher* _dispatcher;  // For air times and errors
  mesh::Radio* _radio;             // For noise floor
  mesh::MainBoard* _board;         // For battery voltage
  mesh::MillisecondClock* _ms;    // For uptime

  // Topic building
  enum MQTTMessageType { MSG_STATUS, MSG_PACKETS, MSG_RAW, MSG_NEIGHBORS };
  bool buildTopicForSlot(int index, MQTTMessageType type, char* topic_buf, size_t buf_size);
  bool substituteTopicTemplate(const char* tmpl, MQTTMessageType type, int slot_index, char* buf, size_t buf_size);
  uint8_t eligiblePacketSlots(uint8_t packet_type, MQTTMessageType type);
  bool shouldQueuePacketType(uint8_t packet_type, bool& filtered);

  // Internal methods - slot management
  // Lifetime model (Phase 1 of MQTT memory-defrag):
  // - ensureSlotClient() allocates this slot's PsychicMqttClient and registers its
  //   persistent callbacks. Called from setupSlot() on a slot's first setup, so an
  //   unconfigured or capped-off slot never pays for a client it cannot use.
  // - destroySlotClients() disconnects and deletes each client. Runs once in end().
  // - setupSlot() ensures the client exists, then configures it (server,
  //   credentials, CA) and calls connect(). Safe to call again to reconfigure.
  // - teardownSlot() only disconnects — it never deletes the client. Leaves
  //   the mbedTLS/transport state ready for a subsequent setupSlot().
  // This avoids delete/new cycles that shed ~40 KB of mbedTLS buffers per
  // reconfigure and fragment the internal heap on non-PSRAM boards.
  bool ensureSlotClient(int index);    // Allocate this slot's persistent client + callbacks on first use
  bool ensureSlotAuthToken(int index); // Allocate this slot's JWT token buffer on first token creation
  void releaseSlotAuthToken(int index);// Free the token buffer (only with the client — see MQTTSlot)
  void destroySlotClients();           // Delete all persistent clients (shutdown only)
  bool setupSlot(int index);           // Configure and connect the slot; false = not activated
  // Single definition of "this slot holds one of the _max_active_slots positions":
  // it is enabled and has been through a successful setupSlot(). Startup, the
  // setup-retry path, and live reconfigure all gate on these so the cap cannot be
  // exceeded by one route while another enforces it.
  int activatedSlotCount() const;
  bool canActivateSlot(int index) const;
  void teardownSlot(int index);        // Disconnect the slot's client (keeps the object alive)
  void maintainSlotConnections();      // Maintain all slot connections (token renewal, reconnect)
  void maintainSlotConnection(int index, unsigned long now_millis, unsigned long current_time, bool time_synced, bool& reconnect_attempted, bool& teardown_attempted);
  bool createSlotAuthToken(int index); // Create/renew JWT token for a slot
  unsigned long slotTokenLifetime(int index) const; // effective JWT lifetime (preset/default minus slot stagger), seconds
  // payload_len is the serialized length the builder already returned. Every caller
  // knows it, and passing it avoids re-scanning up to 2 KB of JSON per destination
  // slot (and up to NEIGHBORS_JSON_BUFFER_SIZE per neighbor snapshot).
  bool publishToSlot(int index, const char* topic, const char* payload, size_t payload_len, bool retained = false, uint8_t qos = 0);
  bool publishToAllSlots(const char* topic, const char* payload, size_t payload_len, bool retained = false, uint8_t qos = 0);
  void publishStatusToSlot(int index);
  void updateCachedConnectionStatus();

  void processPacketQueue();
  bool publishStatus();  // Returns true if status was successfully published
  bool handleWiFiConnection(unsigned long now);

  // FreeRTOS task function (runs on Core 0)
  #ifdef ESP_PLATFORM
  static void mqttTask(void* parameter);
  void mqttTaskLoop();  // Main loop for MQTT task
  void initializeWiFiInTask();  // WiFi initialization moved to task
  #endif
  bool publishPacket(mesh::Packet* packet, bool is_tx, bool& has_eligible_target,
                     const uint8_t* raw_data = nullptr, int raw_len = 0,
                     float snr = 0.0f, float rssi = 0.0f);
  bool publishRaw(mesh::Packet* packet, bool& has_eligible_target);
#if defined(WITH_MQTT_NEIGHBORS)
  // Publishes the pending _neighbors_json_buffer to every connected slot's
  // neighbors topic. Runs on the MQTT task (Core 0) only.
  bool publishNeighbors();
#endif
  void queuePacket(mesh::Packet* packet, bool is_tx);
  void dequeuePacket();
  bool isAnySlotConnected();
  void refreshNTP();  // Lightweight periodic NTP refresh (non-blocking)
  void runNtpDiagProbe();  // Probe every server for connectivity; never sets the clock. Core 0 only.
  // Populates dst_out/std_out with TimeChangeRules for the given IANA or
  // abbreviation string. Returns false if the string is not recognized
  // (callers should fall back to UTC). Zero-allocation.
  static bool timezoneRulesFromString(const char* tz_string, TimeChangeRule& dst_out, TimeChangeRule& std_out);
  void checkConfigurationMismatch();
  bool isIATAValid() const;
  bool isSlotReady(int index, char* reason_buf = nullptr, size_t reason_size = 0) const;

  void optimizeMqttClientConfig(PsychicMqttClient* client, bool needs_large_buffer = false);
  void getClientVersion(char* buffer, size_t buffer_size) const;
  void logMemoryStatus();
  void refreshOriginFromPrefs();
  // begin()/end()-scoped PSRAM buffers. Each allocation is independent so a
  // transient heap shortage degrades to the existing stack fallback instead
  // of making the bridge unusable.
  void allocateRuntimeBuffers();
  void releaseRuntimeBuffers();

  // --- Cooperative lifecycle (Phase 5) ---------------------------------------
  // The pure state machine, bounded stop timeout, and OTA barrier live in
  // src/helpers/MQTTLifecycle.h and are host-tested by test/test_mqtt_lifecycle/.
  // This nested Ops binds that spec to FreeRTOS/PsychicMqttClient. The
  // Coordinator is owned and driven ONLY by the loop task (Core 1) from
  // begin()/end(); the MQTT task (Core 0) communicates solely through the
  // _stop_requested/_stop_acked flags above. Methods are defined in the .cpp.
  class LifecycleOps : public MQTTLifecycle::Ops {
   public:
    explicit LifecycleOps(MQTTBridge* bridge) : _b(bridge) {}
    uint32_t nowMs() override;
    void startTask() override;
    void deliverStop() override;
    void releaseResources() override;
    void onStopComplete(bool clean) override;
   private:
    MQTTBridge* _b;
  };
  LifecycleOps _lifecycle_ops;
  MQTTLifecycle::Coordinator _lifecycle;

  // Observer config (MQTT/WiFi/timezone/SNMP/alert), persisted to /mqtt_prefs.
  // _prefs (held by BridgeBase) still provides upstream fields (freq/sf/node_name…).
  MQTTPrefs* _obs = nullptr;

public:
  MQTTBridge(NodePrefs *prefs, MQTTPrefs *obs, mesh::PacketManager *mgr, mesh::RTCClock *rtc, mesh::LocalIdentity *identity);

  void begin() override;
  void end() override;
  void loop() override;
  void onPacketReceived(mesh::Packet *packet) override;
  void sendPacket(mesh::Packet *packet) override;

  /**
   * Configure a slot with a preset name. Call this when the user runs
   * "set mqttN.preset <name>". Handles teardown of old connection and
   * setup of new one.
   *
   * @param slot_index Slot index (0 to RUNTIME_MQTT_SLOTS-1)
   * @param preset_name Preset name: "analyzer-us", "analyzer-eu", "nz-analyzer", "meshmapper", "custom", "none"
   */
  void setSlotPreset(int slot_index, const char* preset_name);
  void applySlotPreset(int slot_index, const char* preset_name);

  /**
   * Configure custom broker settings for a slot. Only applies when the
   * slot's preset is "custom".
   *
   * @param slot_index Slot index (0 to RUNTIME_MQTT_SLOTS-1)
   * @param host Broker hostname
   * @param port Broker port
   * @param username MQTT username (empty for anonymous)
   * @param password MQTT password (empty for anonymous)
   */
  void setSlotCustomBroker(int slot_index, const char* host, uint16_t port,
                           const char* username, const char* password);

  void setOrigin(const char* origin);
  void setIATA(const char* iata);
  void setDeviceID(const char* device_id);
  void setFirmwareVersion(const char* firmware_version);
  void setBoardModel(const char* board_model);
  void setBuildDate(const char* build_date);
  void storeRawRadioData(const uint8_t* raw_data, int len, float snr, float rssi);
  void setMessageTypes(bool status, bool packets, bool raw);

#if defined(WITH_MQTT_NEIGHBORS)
  // Single source of truth for the neighbors JSON size, used by both the bridge's
  // persistent buffer and the mesh's transient build buffer.
  #if defined(BOARD_HAS_PSRAM)
  static const size_t NEIGHBORS_JSON_BUFFER_SIZE = 10240;
  #else
  // Without PSRAM all three neighbors allocations (persistent buffer, transient
  // build buffer, ArduinoJson pool) come out of internal DRAM, which each TLS
  // slot also needs ~40 KB of. A quarter-size buffer keeps the peak near 13 KB
  // instead of ~35 KB; the builder drops the tail and sets "truncated" once the
  // next entry will not fit, so the table degrades instead of failing.
  static const size_t NEIGHBORS_JSON_BUFFER_SIZE = 4096;
  #endif

  // The ArduinoJson pool is NOT bounded by the text buffer: v7 hands out pool
  // blocks in fixed 4096-byte chunks, so a table that just fits the text buffer
  // can still need well over it in pool. Budgeting the pool at the text size
  // starves it, and a starved pool sets doc.overflowed() — which drops the whole
  // publish instead of truncating. Measured need is 12541 B for 50 entries and
  // 4286 B for 21, so allow a spare block over each.
  #if defined(BOARD_HAS_PSRAM)
  static const size_t NEIGHBORS_DOC_POOL_BUDGET = 16384;
  #else
  static const size_t NEIGHBORS_DOC_POOL_BUDGET = 12288;
  #endif

  // Entries per publish. One 4096-byte pool block holds 21, so staying under
  // that keeps the non-PSRAM pool to a single block (~4.3 KB rather than
  // ~8.4 KB) and the whole publish peak near 13 KB of internal DRAM. PSRAM
  // boards are limited only by the text buffer and the neighbour cache.
  #if defined(BOARD_HAS_PSRAM)
  static const int NEIGHBORS_MAX_PUBLISH_ENTRIES = MAX_NEIGHBOURS;
  #else
  static const int NEIGHBORS_MAX_PUBLISH_ENTRIES = 20;
  #endif

  // Called by the mesh (Core 1) once a neighbor-discovery pass has built the
  // table JSON. Copies it into the persistent PSRAM buffer and raises the
  // publish-pending flag for the MQTT task; a request is dropped if one is
  // already in flight or the buffer is unavailable.
  void requestPublishNeighbors(const char* json, size_t len);

  // Periodic-neighbors schedule, reported by the mesh loop for `get mqtt.status`.
  // The mesh owns the timer; the bridge only caches the summary so the wrap-safe
  // millis math stays on the side that already has those helpers.
  enum NeighborsPhase : uint8_t {
    NBR_SCHEDULED,  // waiting for the next publish; secs_until_next is valid
    NBR_ACTIVE,     // zero-hop refresh or scope queries in flight
    NBR_DUE,        // publish is due, waiting on the bridge/WiFi to come up
  };
  void setNeighborsSchedule(NeighborsPhase phase, uint32_t secs_until_next);
#endif

  int getConnectedBrokers() const;
  int getQueueSize() const;
  bool isReady() const;
  /** True only after a CLEAN cooperative stop — end() received the MQTT task's
   *  acknowledgment within the timeout. A timed-out/forced stop returns false so
   *  OTA flashing is withheld until a clean start/stop cycle. Mirrors
   *  MQTTLifecycle::mayBeginFlash(); read on the loop task (Core 1). */
  bool canFlashAfterStop() const { return _lifecycle.mayBeginFlash(); }

  static unsigned long getWifiConnectedAtMillis();

  /**
   * Per-slot outage accessors used by AlertReporter to detect prolonged
   * MQTT broker outages. Indices are 0..RUNTIME_MQTT_SLOTS-1.
   *
   * - getSlotCurrentOutageStartMs(): millis() of the current outage start
   *   (0 when the slot is connected). Reset on each reconnect.
   * - isSlotEnabledAndAttempted(): true when the slot is enabled (preset
   *   != "none") and has reached at least one connect attempt — i.e. it is
   *   meaningful to alarm on its connection state.
   * - getSlotPresetName(): preset name for friendly status text. Returns
   *   "custom"/"none"/preset->name; never null.
   */
  unsigned long getSlotCurrentOutageStartMs(int slot_index) const;
  bool isSlotEnabledAndAttempted(int slot_index) const;
  const char* getSlotPresetName(int slot_index) const;
  static int getRuntimeSlotCount() { return RUNTIME_MQTT_SLOTS; }
  /** Max slots that can be connected at once: 5 with PSRAM, 2 without (each
   *  WSS/TLS connection needs ~40KB for mbedTLS buffers). This is the number of
   *  usefully-configurable servers; RUNTIME_MQTT_SLOTS carries a spare for
   *  reconfiguration. Safe to call before begin(). */
  static int getMaxActiveSlots();
  /** Resolved origin for MQTT JSON: node_name when mqtt_origin is empty, else mqtt_origin (with quote stripping). */
  static void getEffectiveMqttOrigin(const NodePrefs* np, const MQTTPrefs* obs, char* buf, size_t buf_size);
  static const char* effectiveNtpPrimary(const MQTTPrefs* obs);
  /** Sync system clock via NTP. force=true bypasses the 5s post-sync rate limit.
   *  primary_only=true tests just the effective primary server (no fallback walk) so a
   *  mistyped hostname fails fast instead of blocking through the whole fallback list.
   *  Performs blocking NTP I/O and must only be called from the MQTT task (Core 0).
   *  Other tasks (e.g. the CLI on Core 1) must use requestForcedNtpSync() instead. */
  bool syncTimeWithNTP(bool force = false, bool primary_only = false);
  /** Request a forced NTP sync from another task (e.g. CLI on Core 1). Marshals the
   *  work onto the MQTT task so all NTP I/O stays on Core 0, then blocks up to
   *  timeout_ms for the result. Returns true if the sync succeeded, false on failure,
   *  timeout, or if the bridge is not running. */
  bool requestForcedNtpSync(uint32_t timeout_ms = 30000);
  /** Probe every configured NTP server (custom primary + built-in fallbacks) for
   *  connectivity and report each server's reported time WITHOUT touching the system
   *  clock. Marshals the probe onto the MQTT task (Core 0), then formats on the caller's
   *  thread. verbose=true prints a detailed table to the serial console and leaves a short
   *  summary in reply; verbose=false fills reply with a compact "<server> ok|fail" list
   *  (for LoRa). Returns false if the bridge is not running. */
  bool ntpDiag(char* reply, size_t reply_size, bool verbose);
  static void formatMqttStatusReply(char* buf, size_t bufsize, const MQTTPrefs* obs);
  /** On-demand publish-health + heap snapshot for `get mqtt.stats` (per-slot ok/err,
   *  outbox size, free/max heap, queue depth). */
  static void formatMqttStatsReply(char* buf, size_t bufsize);
  /** Structured per-slot snapshot for the webconfig stats endpoint. Same state
   *  logic as formatMqttStatusReply, same cross-task read semantics. Returns
   *  false when the bridge is not running, the index is out of range, or the
   *  slot is unconfigured (skip it). */
  struct SlotStatusSnapshot {
    const char* name;    // preset name or "custom"
    const char* state;   // "inactive" | "wait" | "ok" | "fail" | "disc"
    unsigned long publish_ok;
    unsigned long publish_err;
    // Raw packet-type allowlist. Kept as the mask rather than canonical text so
    // the 1 KB stats JSON stays compact; the portal renders it.
    uint16_t filter_mask;
  };
  static bool getSlotStatusSnapshot(int slot_index, SlotStatusSnapshot* out);
  /** True when WiFi is set and at least one MQTT slot can run (preset + custom host if needed). */
  static bool isConfigValid(const MQTTPrefs* obs);
  static void formatSlotDiagReply(char* buf, size_t bufsize, int slot_index);
  static uint8_t getLastWifiDisconnectReason();
  static unsigned long getLastWifiDisconnectTime();
  static const char* wifiReasonStr(uint8_t reason);
  static const char* tlsErrorStr(int32_t err);

  void setStatsSources(mesh::Dispatcher* dispatcher, mesh::Radio* radio,
                       mesh::MainBoard* board, mesh::MillisecondClock* ms);

#ifdef WITH_SNMP
  void setSNMPAgent(MeshSNMPAgent* agent) { _snmp_agent = agent; }
#endif
};

#endif
