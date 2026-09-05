#pragma once

// Browser-based configuration portal for ESP32 MQTT observer builds.
//
// Two modes:
//  - SETUP: open SoftAP + captive portal, raised automatically on first boot
//    when no WiFi is configured (wifi_ssid empty), or manually via
//    `start webconfig ap`. Save -> reboot; auto-stops after an idle timeout.
//  - LAN: bound to the existing STA connection (owned by the MQTT bridge
//    task), started via `start webconfig`, admin-password login required.
//
// Concurrency model: AsyncWebServer handlers run on the async_tcp task and
// must never touch the CLI, prefs persistence, or the radio. Config writes
// are marshaled into a single-slot command batch that tick() - called from
// MyMesh::loop() on the Arduino loop task - drains through the existing CLI
// `set` handlers. Prefs-struct reads and batch state are guarded by _mux.
// The HTTP server/routes live for the firmware lifetime; routes acquire the
// currently attached WebConfigServer session only for the synchronous handler
// call. This lets a stopped session be reclaimed without deleting an
// AsyncWebServer that may still be referenced by a partially received request.
//
// No persisted state: everything here is RAM-only, so prefs-file layouts
// (fleet-critical) are untouched.

#if defined(ESP_PLATFORM) && defined(WITH_MQTT_BRIDGE)

#define WITH_WEBCONFIG 1

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <helpers/WebConfigBatch.h>

class AsyncWebServer;
class AsyncWebServerRequest;
class DNSServer;
struct NodePrefs;
struct MQTTPrefs;

#ifndef WEBCONFIG_AP_IDLE_TIMEOUT_MS
  #define WEBCONFIG_AP_IDLE_TIMEOUT_MS  (10UL * 60UL * 1000UL)
#endif
#ifndef WEBCONFIG_SESSION_TTL_MS
  #define WEBCONFIG_SESSION_TTL_MS      (20UL * 60UL * 1000UL)
#endif

class WebConfigServer {
public:
  enum Mode : uint8_t { MODE_OFF = 0, MODE_SETUP, MODE_LAN };

  class Callbacks {
  public:
    // Run one CLI command. Called from tick() only (loop task); reply is 160 bytes.
    virtual void execCommand(char* cmd, char* reply) = 0;
    virtual void rebootNow() = 0;
    // Bracket a config batch so bridge restarts triggered by individual
    // `set` handlers can be coalesced into one.
    virtual void onConfigBatchStart() {}
    virtual void onConfigBatchEnd() {}
    // Fill buf with the stats JSON snapshot. Called from tick() (loop task).
    virtual void buildStatsJson(char* buf, size_t buf_size) = 0;
    // Teardown finished (session + DNS freed, WiFi mode restored).
    virtual void onWebConfigStopped() {}
  };

  WebConfigServer(NodePrefs* prefs, MQTTPrefs* obs, Callbacks* callbacks,
                  const uint8_t* pub_key, const char* fw_ver, const char* build_date,
                  const char* role, const char* board_name);
  ~WebConfigServer();

  // For the device display: true while a setup-mode portal is active.
  // Fills the AP SSID and portal IP; either buffer may be NULL to just poll.
  // Call from the loop task only (same task that changes the mode).
  static bool getSetupInfo(char* ssid, size_t ssid_len, char* ip, size_t ip_len);

  // For the device display: true once a config save completed and the node is
  // about to reboot — ground truth for the user even if the browser lost its
  // connection before the confirmation arrived. Loop task only.
  static bool isRebootPending();

  bool startSetupMode(char reply[]);   // open SoftAP + DNS captive portal
  bool startLanMode(char reply[]);     // bind to existing STA connection
  void requestStop();                  // stop listening and detach this session
  void tick(uint32_t now);             // call every loop iteration

  Mode mode() const { return _mode; }
  bool isRunning() const { return _mode != MODE_OFF; }
  bool isStopping() const { return _stopping; }

private:
  // Batch/reboot/stop decisions and timings live in the fork-owned pure spec
  // WebConfigBatch.h (host-tested by test/test_webconfig_batch). These aliases
  // keep a single source of truth so the spec and this server cannot drift.
  static const int MAX_BATCH = WebConfigBatch::kMaxBatch;
  static const size_t MAX_BODY = 4096;
  // A detached session normally drains immediately because handlers are short.
  // If one does not, keep the session alive (safe) and emit a diagnostic rather
  // than freeing memory still referenced by the async task.
  static const uint32_t STOP_WARN_MS = WebConfigBatch::kStopWarnMs;
  enum BatchState : uint8_t { BATCH_IDLE = 0, BATCH_PENDING, BATCH_DONE };
  // What filled the shared slot. A config save comes from allowlisted form
  // fields; a CLI sequence is arbitrary commands typed into the terminal. They
  // share the slot (see WebConfigBatch.h) but differ in how results are read
  // and in whether `key` means anything, so every reader checks the kind.
  enum BatchKind : uint8_t { BATCH_CONFIG = 0, BATCH_CLI };

  // BatchState and WebConfigBatch::State are deliberately kept as separate
  // types (the enum is stored in a volatile member and used in prints); this
  // is the one conversion point.
  static WebConfigBatch::State toSpecState(BatchState s) {
    switch (s) {
      case BATCH_PENDING: return WebConfigBatch::State::Pending;
      case BATCH_DONE:    return WebConfigBatch::State::Done;
      default:            return WebConfigBatch::State::Idle;
    }
  }
  struct BatchEntry {
    char key[24];     // allowlisted config key (echoed back to the UI); empty for CLI entries
    char cmd[160];    // full CLI command (may contain secrets - never echoed)
    char reply[160];  // CLI reply budget, same 160 bytes the serial console gets
  };

  NodePrefs* _prefs;
  MQTTPrefs* _obs;
  Callbacks* _cb;
  const uint8_t* _pub_key;
  const char* _fw_ver;
  const char* _build_date;
  const char* _role;
  const char* _board_name;

  AsyncWebServer* _server = NULL;
  DNSServer* _dns = NULL;
  SemaphoreHandle_t _mux;
  Mode _mode = MODE_OFF;
  bool _stopping = false;
  bool _was_setup_ap = false;
  bool _initial_setup = false;
  // A `password` command has succeeded this session. Lets the CLI satisfy the
  // initial-setup invariant across separate submissions; the form batch always
  // sends the password with the rest, so it never needed the memory.
  bool _admin_pwd_set = false;
  char _ap_ssid[33] = {0};

  // Currently attached session, also used by the display's setup-info poll.
  static WebConfigServer* _active;
  // Process-lifetime listener and route table. Requests retain a pointer to the
  // AsyncWebServer internally until disconnect, so this object is deliberately
  // never deleted during normal firmware operation.
  static AsyncWebServer* _host;

  // Command batch: filled by async_tcp under _mux, drained by tick().
  volatile BatchState _batch_state = BATCH_IDLE;
  volatile BatchKind _batch_kind = BATCH_CONFIG;
  uint8_t _batch_count = 0;
  uint8_t _batch_next = 0;        // drain progress (one command per tick)
  uint32_t _batch_last_cmd = 0;
  bool _batch_reboot = false;
  bool _batch_reboot_armed = false;
  bool _batch_all_ok = true;      // every drained command replied "OK" (gates reboot)
  // Client-supplied batch identity, echoed in the 202/result/409 responses so a
  // reused/lost/concurrent result can't be mistaken for this client's own.
  char _batch_reqid[24] = {0};
  BatchEntry _batch[MAX_BATCH];

  // LAN-mode session (single slot; new login evicts the old session)
  char _session_token[33] = {0};
  uint32_t _session_last_seen = 0;
  uint8_t _login_fails = 0;
  uint32_t _login_lock_until = 0;

  // Once set (via /api/portal/exit), OS captive probes get native "success"
  // replies so the phone's sign-in sheet can be dismissed without dropping the
  // WiFi, letting the user continue in their real browser. async_tcp-task only.
  volatile bool _captive_release = false;

  // Save-path diagnostics: 1 Hz serial trace for 60 s after each config POST
  // (AP station count, heap, batch state) to pinpoint client drops on hardware.
  volatile uint32_t _diag_until = 0;
  uint32_t _diag_last = 0;

  volatile uint32_t _last_activity = 0;
  uint32_t _reboot_at = 0;         // 0 = none scheduled
  uint32_t _stop_warn_at = 0;
  bool _stop_warned = false;
  // Number of synchronous route handlers currently using this session. Access
  // is serialized by the file-local route spinlock in WebConfigServer.cpp.
  uint32_t _handler_refs = 0;
  volatile uint32_t _stats_wanted_until = 0;
  uint32_t _stats_built_at = 0;
  char _stats_json[1024] = {0};

  void createServer();
  void registerRoutes();
  typedef void (WebConfigServer::*RequestHandler)(AsyncWebServerRequest*);
  static void dispatchRequest(AsyncWebServerRequest* req, RequestHandler handler);
  void attachRoutes();
  void detachRoutes();
  uint32_t handlerRefCount() const;
  void drainBatch(uint32_t now);
  void finalizeTeardown();
  bool checkAuth(AsyncWebServerRequest* req);
  static void collectBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                          size_t index, size_t total);

  void handleRoot(AsyncWebServerRequest* req);
  void handleStatus(AsyncWebServerRequest* req);
  void handleLogin(AsyncWebServerRequest* req);
  void handleLogout(AsyncWebServerRequest* req);
  void handleConfigGet(AsyncWebServerRequest* req);
  void handleConfigPost(AsyncWebServerRequest* req);
  void handleConfigResult(AsyncWebServerRequest* req);
  void handleCliPost(AsyncWebServerRequest* req);
  void handleCliResult(AsyncWebServerRequest* req);
  void handleStats(AsyncWebServerRequest* req);
  void handleScan(AsyncWebServerRequest* req);
  void handlePresets(AsyncWebServerRequest* req);
  void handleReboot(AsyncWebServerRequest* req);
  void handlePortalExit(AsyncWebServerRequest* req);
  void handleNotFound(AsyncWebServerRequest* req);
};

#endif  // ESP_PLATFORM && WITH_MQTT_BRIDGE
