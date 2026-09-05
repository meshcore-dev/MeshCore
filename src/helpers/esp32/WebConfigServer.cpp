#if defined(ESP_PLATFORM) && defined(WITH_MQTT_BRIDGE)

#include "WebConfigServer.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

#include <helpers/CommonCLI.h>
#include <helpers/MQTTPacketFilter.h>
#include <helpers/MQTTPresets.h>
#include <helpers/WebConfigKeys.h>
#include <helpers/bridges/MQTTBridge.h>

#include "WebConfigHtml.h"

// Placeholder sent instead of stored secrets; POSTs carrying it are dropped
// so an untouched password field never overwrites the stored value.
static const char SECRET_SENTINEL[] = "********";

// Key classification (allowlist, secret detection, slot-prefix parsing) lives in
// helpers/WebConfigKeys.h so it can be unit-tested on the host. Thin aliases keep
// the call sites below readable.
static inline bool isAllowedSetKey(const char* key) { return wcIsAllowedSetKey(key); }
static inline bool isSecretKey(const char* key) { return wcIsSecretKey(key); }

// Commands whose CLI handler never returns would take the node down mid-drain,
// before the client could read a single result. `reboot` is deferred instead:
// it is not passed to the CLI at all, and the batch arms the ordinary reboot
// path once the operator has read the results. The rest (clkreboot, poweroff,
// ota update) do real work on the way down and cannot be faked, so they run
// normally and the connection drops — the UI warns before sending them.
// CommonCLI dispatches on a 6-byte PREFIX (memcmp(command, "reboot", 6)), so
// `reboot now` and `rebooted` reach Board::reboot() too. Matching exactly here
// let those through to the CLI, which took the node down mid-drain with no
// results and no deferral — the precise failure the deferral exists to avoid.
// Whatever CommonCLI would treat as a reboot, this must intercept.
static inline bool wcIsDeferredReboot(const char* cmd) {
  return strncmp(cmd, "reboot", 6) == 0;
}

// Commands the CLI reaches but the portal cannot honestly serve. Rejected at
// POST so nothing in the sequence runs, rather than failing halfway with a
// reply that does not explain itself. Returns the reason, or NULL if fine.
static const char* wcCliUnavailable(const char* cmd) {
  // ESP32Board::startOTAUpdate() does `new AsyncWebServer(80)` with no bind
  // check and answers "Started" regardless. The portal already holds port 80,
  // so from here it can only leak the allocation, inhibit sleep, and lie.
  if (strncmp(cmd, "start ota", 9) == 0) {
    return "start ota needs port 80, which this portal is using. "
           "Run it from the serial console, or use `ota update`.";
  }
  // `clock sync` sets the clock from the CALLER's timestamp. Web requests carry
  // none (execCommand passes 0), so CommonCLI always rejects it as moving the
  // clock backwards. `time <epoch>` is the one that works over this transport.
  if (strncmp(cmd, "clock sync", 10) == 0) {
    return "clock sync takes its time from the caller, which a web request has "
           "no way to supply. Use `time <epoch-seconds>` instead.";
  }
  // Both write their real output to Serial and hand back a stub the terminal
  // would render as success. Bare `log` also streams a whole file from the loop
  // task, stalling the mesh and the radio while it does.
  if (strcmp(cmd, "log") == 0) {
    return "log writes the packet log to the serial console, not here, and "
           "blocks the radio while it does. Use `log start` / `log stop`.";
  }
  if (strcmp(cmd, "get acl") == 0) {
    return "get acl writes to the serial console, not here.";
  }
  return NULL;
}
// The `password` command echoes the new password back in its reply, and replies
// are served to the client over the open setup AP. The config path overwrites it
// by key; a CLI entry has no key, so match on the command itself.
static inline bool wcCliEchoesSecret(const char* cmd) {
  return strncmp(cmd, "password ", 9) == 0;
}

// CommonCLI splits its surface by CALLER, not by command: a serial caller
// (sender_timestamp 0, physical access) reads secrets in plaintext, while a
// remote one gets "******** (serial only)". Its own comments say so — "Serial
// only (WiFi creds grant LAN access); remote sees set/unset".
//
// execCommand passes 0, which is what makes `erase`, `stats-*` and `set freq`
// reachable from the terminal at all. Left alone, that also claims
// physical-access trust for an HTTP request: `get prv.key` would hand this
// node's identity to anyone associated with the open setup AP, and `get
// wifi.pwd` would hand over the operator's network. Reading a secret and
// writing one are not the same capability — the wizard has always been able to
// REPLACE these; nothing in the portal could ever READ them, because
// /api/config masks them (wcIsSecretKey).
//
// So the command surface stays whole and only the READ is masked, restoring the
// distinction CommonCLI intended for a caller who is not at the serial port.
// Which commands those are lives in WebConfigKeys.h (wcIsSecretReadCommand),
// beside the rest of the secret classification and host-tested with it.

// Keep the set/unset signal, which is the useful part and what CommonCLI itself
// reports remotely; only the value goes. A getter answers "> value".
static void wcMaskSecretReply(char* reply) {
  const char* val = reply;
  if (val[0] == '>' && val[1] == ' ') val += 2;
  const bool unset = (val[0] == 0 || strcmp(val, "(not set)") == 0);
  strcpy(reply, unset ? "> (not set)" : "> ******** (serial only)");
}
// Reply classification lives in WebConfigBatch.h with the rest of the decisions
// (WebConfigBatch::cliReplyIsFailure / cliReplyGatesReboot / cliWriteSucceeded),
// so the shapes CommonCLI actually emits are enumerated in one host-tested place.

// Constant-time-ish comparison so login timing doesn't leak a prefix match.
static bool fixedTimeEquals(const char* a, const char* b, size_t max_len) {
  size_t la = strnlen(a, max_len), lb = strnlen(b, max_len);
  uint8_t diff = (la == lb) ? 0 : 1;
  for (size_t i = 0; i < max_len; i++) {
    char ca = (i < la) ? a[i] : 0;
    char cb = (i < lb) ? b[i] : 0;
    diff |= (uint8_t)(ca ^ cb);
  }
  return diff == 0;
}

// RAII lock; tolerates a null mutex (allocation failure) by not locking.
struct WCLock {
  SemaphoreHandle_t h;
  explicit WCLock(SemaphoreHandle_t s) : h(s) { if (h) xSemaphoreTake(h, portMAX_DELAY); }
  ~WCLock() { if (h) xSemaphoreGive(h); }
};

WebConfigServer* WebConfigServer::_active = NULL;
AsyncWebServer* WebConfigServer::_host = NULL;

// Out-of-line definitions for the in-class-initialised constants. An in-class
// initialiser is only a declaration under C++11 (what the xtensa-esp32
// toolchain builds with), so any use that binds a reference rather than reading
// the value — ArduinoJson takes its argument as `const T&` — needs the symbol to
// exist. Comparisons like `count >= MAX_BATCH` never did, which is why this only
// surfaced when MAX_BATCH started being reported in JSON, and then only on the
// targets where the compiler happened not to fold it.
const int WebConfigServer::MAX_BATCH;
const size_t WebConfigServer::MAX_BODY;
const uint32_t WebConfigServer::STOP_WARN_MS;

// Protects the permanent route host's active-session pointer and handler
// references across the loop and async_tcp cores. The critical sections only
// copy a pointer/update a counter; handlers themselves never run under it.
static portMUX_TYPE s_wc_route_mux = portMUX_INITIALIZER_UNLOCKED;

WebConfigServer::WebConfigServer(NodePrefs* prefs, MQTTPrefs* obs, Callbacks* callbacks,
                                 const uint8_t* pub_key, const char* fw_ver,
                                 const char* build_date,
                                 const char* role, const char* board_name)
    : _prefs(prefs), _obs(obs), _cb(callbacks), _pub_key(pub_key),
      _fw_ver(fw_ver), _build_date(build_date), _role(role), _board_name(board_name) {
  _mux = xSemaphoreCreateMutex();
}

WebConfigServer::~WebConfigServer() {
  detachRoutes();
  if (_mux) vSemaphoreDelete(_mux);
}

bool WebConfigServer::isRebootPending() {
  WebConfigServer* w = _active;
  return w != NULL && WebConfigBatch::isConfigRebootPending(
                          w->_reboot_at, w->_batch_reboot,
                          toSpecState(w->_batch_state));
}

bool WebConfigServer::getSetupInfo(char* ssid, size_t ssid_len, char* ip, size_t ip_len) {
  WebConfigServer* w = _active;
  if (w == NULL || w->_mode != MODE_SETUP || w->_stopping) return false;
  if (ssid && ssid_len > 0) {
    strncpy(ssid, w->_ap_ssid, ssid_len - 1);
    ssid[ssid_len - 1] = 0;
  }
  if (ip && ip_len > 0) {
    snprintf(ip, ip_len, "%s", WiFi.softAPIP().toString().c_str());
  }
  return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool WebConfigServer::startSetupMode(char reply[]) {
  if (_mode != MODE_OFF || _stopping) {
    strcpy(reply, "Err: webconfig busy");
    return false;
  }
  // AP_STA (not pure AP) so the WiFi scan for the SSID picker works while
  // the AP is up. STA stays unconnected - the bridge won't touch WiFi
  // while wifi_ssid is empty, and `start webconfig ap` requires it stopped.
  WiFi.mode(WIFI_AP_STA);
  // Setup mode serves an UNAUTHENTICATED API on the trust of physical AP
  // proximity. `start webconfig ap` can be run with the STA still associated
  // to the operator's LAN (MQTTBridge::end() leaves the STA link up and
  // auto-reconnect on), which would expose that open API to every host on the
  // LAN. Drop the association and disable auto-reconnect so only the SoftAP
  // interface is reachable; the STA interface itself stays up (unassociated)
  // purely so WiFi.scanNetworks() below can populate the SSID picker.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false /*keep radio on*/, true /*erase stored AP so it can't reconnect*/);
  snprintf(_ap_ssid, sizeof(_ap_ssid), "MeshCore-Setup-%02X%02X", _pub_key[0], _pub_key[1]);
#ifdef WEBCONFIG_AP_PASSWORD
  bool ap_ok = WiFi.softAP(_ap_ssid, WEBCONFIG_AP_PASSWORD);
#else
  bool ap_ok = WiFi.softAP(_ap_ssid);
#endif
  if (!ap_ok) {
    WiFi.mode(WIFI_OFF);
    strcpy(reply, "Err: failed to start AP");
    return false;
  }
  delay(100);  // let the AP netif settle before reading its IP
  IPAddress ip = WiFi.softAPIP();

  _dns = new DNSServer();
  _dns->start(53, "*", ip);  // captive portal: every name resolves to us

  _mode = MODE_SETUP;
  _initial_setup = (_obs->wifi_ssid[0] == 0);
  createServer();
  _was_setup_ap = true;
  _last_activity = millis();
  WiFi.scanNetworks(true);  // pre-populate the SSID picker

  sprintf(reply, "WebConfig AP started: join '%s' then open http://%s/", _ap_ssid, ip.toString().c_str());
  return true;
}

bool WebConfigServer::startLanMode(char reply[]) {
  if (_mode != MODE_OFF || _stopping) {
    strcpy(reply, "Err: webconfig busy");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    strcpy(reply, "Err: WiFi not connected");
    return false;
  }
  _mode = MODE_LAN;
  createServer();
  _last_activity = millis();

  int pos = sprintf(reply, "WebConfig started: http://%s/ (admin password login)",
                    WiFi.localIP().toString().c_str());
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < 60 * 1024) {
    sprintf(reply + pos, " WARN: low heap");
  }
  return true;
}

void WebConfigServer::createServer() {
  if (_host == NULL) {
    _host = new AsyncWebServer(80);
    _server = _host;
    registerRoutes();
  } else {
    _server = _host;
  }
  // iOS caches plain-HTTP GETs aggressively (keyed by URL, surviving even a
  // device reflash behind the same IP), which poisons /api/config/result and
  // friends with stale responses from earlier sessions. Forbid caching on
  // every response; the HTML is small enough to refetch per visit.
  //
  // DefaultHeaders::Instance() is a process-lifetime singleton and addHeader()
  // appends unconditionally, so registering here on every start/stop cycle
  // would leak heap and stack duplicate "Cache-Control" headers on every
  // response. Register exactly once for the firmware lifetime.
  static bool s_default_headers_registered = false;
  if (!s_default_headers_registered) {
    DefaultHeaders::Instance().addHeader("Cache-Control", "no-store");
    s_default_headers_registered = true;
  }
  attachRoutes();
  _server->begin();
}

void WebConfigServer::requestStop() {
  if (_mode == MODE_OFF && !_stopping) return;
  // Detach first. Any request whose headers/body finish parsing after this point
  // is dispatched by the permanent host as 503 and never sees this session.
  detachRoutes();
  if (_server) _server->end();
  if (_dns) _dns->stop();
  _mode = MODE_OFF;
  _stopping = true;
  _stop_warn_at = WebConfigBatch::scheduleAt(millis(), STOP_WARN_MS);
  _stop_warned = false;
}

void WebConfigServer::finalizeTeardown() {
  // `_host` and its routes are process-lifetime. AsyncWebServerRequest retains
  // its server pointer until disconnect, so deleting the host here cannot be
  // made safe using route-level request counts. Only the per-run session is
  // reclaimed by MyMesh after this method reports completion.
  //
  // Design note (reverses the earlier free-on-stop approach): stop used to
  // `delete _server` here once an in-flight request count hit zero, gated by a
  // settle window and a hard cap. That could never be race-free - the hard cap
  // freed the server even with a request still in flight, and because the
  // request holds the server pointer until it disconnects, no request count can
  // close that window. The disconnect UAF that motivated this file lived
  // exactly there. We now keep the AsyncWebServer resident for the firmware
  // lifetime (a small, bounded permanent cost: the server object + route table;
  // the ~10 KB session with its _batch[] array is still reclaimed, and the
  // listener socket is released by _server->end() on stop) and route requests
  // through the currently attached session, so nothing a live request points at
  // is ever freed.

  _server = NULL;
  delete _dns;
  _dns = NULL;
  if (_was_setup_ap) {
    WiFi.softAPdisconnect(true);
    // Nothing else owns WiFi when we raised the AP: either the node is
    // unconfigured, or `start webconfig ap` required the bridge stopped.
    if (_obs->wifi_ssid[0] == 0) {
      WiFi.mode(WIFI_OFF);
    } else {
      WiFi.mode(WIFI_STA);
    }
    _was_setup_ap = false;
  }
  _initial_setup = false;
  _stopping = false;
  _stop_warn_at = 0;
  _stop_warned = false;
  _reboot_at = 0;
  _batch_state = BATCH_IDLE;
  _batch_next = 0;
  _batch_reboot_armed = false;
  _session_token[0] = 0;
  _stats_json[0] = 0;
  if (_cb) _cb->onWebConfigStopped();
}

void WebConfigServer::tick(uint32_t now) {
  if (_stopping) {
    uint32_t refs = handlerRefCount();
    switch (WebConfigBatch::stopStep(refs, _stop_warned, _stop_warn_at, now)) {
      case WebConfigBatch::StopAction::Finalize:
        finalizeTeardown();
        break;
      case WebConfigBatch::StopAction::Warn:
        _stop_warned = true;
        Serial.printf("WC: stop waiting for %lu handler(s); retaining session safely\n",
                      (unsigned long)refs);
        break;
      case WebConfigBatch::StopAction::Wait:
        break;
    }
    return;
  }
  if (_mode == MODE_OFF) return;

  if (_dns) _dns->processNextRequest();

  if (_batch_state == BATCH_PENDING) drainBatch(now);

  if (WebConfigBatch::rebootDue(_reboot_at, now)) {
    Serial.printf("WC: rebooting now (%s)\n", _batch_reboot_armed ? "confirmed" : "fallback");
    _cb->rebootNow();  // does not return
  }

  if ((int32_t)(_diag_until - now) > 0 && (now - _diag_last) >= 1000) {
    _diag_last = now;
    Serial.printf("WC: diag sta=%d heap=%u batch=%d/%d state=%d\n",
                  (int)WiFi.softAPgetStationNum(), (unsigned)ESP.getFreeHeap(),
                  (int)_batch_next, (int)_batch_count, (int)_batch_state);
  }

  // Refresh the stats snapshot only while a client is actually polling.
  if ((int32_t)(_stats_wanted_until - now) > 0 && (now - _stats_built_at) >= 2000) {
    WCLock lock(_mux);
    _cb->buildStatsJson(_stats_json, sizeof(_stats_json));
    _stats_built_at = now;
  }

  // Idle timeout: only the setup AP auto-stops (a deployed node must not be
  // left broadcasting an open AP). LAN mode runs until `stop webconfig`.
  if (_mode == MODE_SETUP && WiFi.softAPgetStationNum() == 0 &&
      (now - _last_activity) > WEBCONFIG_AP_IDLE_TIMEOUT_MS) {
    requestStop();
  }
}

void WebConfigServer::drainBatch(uint32_t now) {
  // One command per call, spaced out. Each `set` persists prefs with a flash
  // write, and flash writes stall the WiFi task (flash cache off); running a
  // whole batch back-to-back starves the softAP of beacons long enough for
  // clients (iPhones especially) to drop off mid-save.
  if (_batch_next == 0) {
    _cb->onConfigBatchStart();
  } else if (WebConfigBatch::drainMustWait(_batch_next, _batch_count, now, _batch_last_cmd)) {
    return;  // let the WiFi task breathe between flash writes
  }
  if (_batch_next < _batch_count) {
    BatchEntry& e = _batch[_batch_next++];
    e.reply[0] = 0;
    uint32_t t0 = millis();
    // Hold _mux across the setter: it mutates the same _prefs/_obs strings that
    // handleConfigGet() serializes on the async_tcp task, so without the lock
    // the "read" side is unprotected and a GET can observe a half-written value.
    // One command per tick keeps the hold brief; the flash write inside stalls
    // WiFi regardless, so a concurrent GET waiting on it costs nothing extra.
    {
      WCLock lock(_mux);
      // A CLI `reboot` is answered here rather than executed: Board::reboot()
      // does not return, so running it would take the node down before the
      // operator could read whether the preceding commands succeeded. The
      // batch arms the ordinary deferred reboot on the first result read.
      if (_batch_kind == BATCH_CLI && wcIsDeferredReboot(e.cmd)) {
        // Deliberately non-committal: whether the reboot actually happens is
        // not known until the whole sequence has run (it is withheld if any
        // command failed), and a later command could still fail after this one.
        strcpy(e.reply, "OK - reboot queued");
      } else {
        _cb->execCommand(e.cmd, e.reply);
      }
      if (e.reply[0] == 0) strcpy(e.reply, "OK");
      // The upstream `password` command echoes the new password back in its
      // reply, and replies are served to the client over the open setup AP.
      // Overwrite it: the command cannot fail, so there is nothing to report.
      // Config entries carry the key; a CLI entry is matched on the command.
      const bool set_admin_pwd = wcIsAdminPasswordKey(e.key) || wcCliEchoesSecret(e.cmd);
      if (set_admin_pwd) strcpy(e.reply, "OK");
      // Satisfies the initial-setup invariant for the rest of this session, so
      // the operator can set the password and configure WiFi in separate steps
      // (the form batch always sends them together and needs no such memory).
      if (set_admin_pwd) _admin_pwd_set = true;
      if (_batch_kind == BATCH_CLI && wcIsSecretReadCommand(e.cmd)) wcMaskSecretReply(e.reply);
      // What gates the reboot is narrower than "did anything fail": only a
      // write can leave the node in a config not worth rebooting into, and only
      // a write has a reply convention ("OK") solid enough to test. Diagnostics
      // in the sequence neither gate it nor get guessed at.
      if (_batch_kind != BATCH_CLI || WebConfigBatch::cliReplyGatesReboot(e.cmd)) {
        _batch_all_ok = WebConfigBatch::nextAllOk(
            _batch_all_ok, WebConfigBatch::cliWriteSucceeded(e.reply));
      }
    }
    _batch_last_cmd = millis();
    // Config entries are named by their (non-secret) key. A CLI command is
    // deliberately not logged: the operator can see what they typed, and a
    // `set wifi.pwd` or `password` from the terminal must not reach the serial
    // log, which is a different audience from the browser session.
    if (_batch_kind == BATCH_CLI) {
      Serial.printf("WC: cli %d/%d took %lums\n", (int)_batch_next, (int)_batch_count,
                    (unsigned long)(_batch_last_cmd - t0));
    } else {
      Serial.printf("WC: cmd %d/%d '%s' took %lums\n", (int)_batch_next, (int)_batch_count,
                    e.key, (unsigned long)(_batch_last_cmd - t0));
    }
    if (!WebConfigBatch::drainFinished(_batch_next, _batch_count)) {
      return;  // more commands next tick
    }
  }
  _cb->onConfigBatchEnd();
  WCLock lock(_mux);
  _batch_state = BATCH_DONE;
  // Assign only when a reboot is actually scheduled. finishRebootAt() returns 0
  // for "not scheduled", but _reboot_at is NOT solely batch-owned: the manual
  // /api/reboot route can arm it from the async_tcp task while a batch is still
  // draining, and an unconditional assign here would silently cancel it.
  if (WebConfigBatch::finishRebootAt(_batch_reboot, _batch_all_ok, now) != 0) {
    // Fallback only, and only when every command succeeded: rebooting into a
    // partially-applied config would strand the node. The real 3 s reboot timer
    // is armed when the client reads /api/config/result (handleConfigResult), so
    // the browser gets its confirmation before the AP/WiFi drops. This covers a
    // client that disconnected and never polls — generous enough for a phone
    // that got bounced off the AP mid-save to rejoin and fetch its confirmation.
    _reboot_at = WebConfigBatch::finishRebootAt(_batch_reboot, _batch_all_ok, now);
  }
}

// ---------------------------------------------------------------------------
// Routes / auth
// ---------------------------------------------------------------------------

// Diagnostic trace of every request that reaches the server (async_tcp task).
// Distinguishes "client stopped sending" from "server stopped accepting" when
// a save's confirmation polls go missing on hardware.
static void wcLogReq(AsyncWebServerRequest* r) {
  Serial.printf("WC: http %s %s\n", r->methodToString(), r->url().c_str());
}

void WebConfigServer::attachRoutes() {
  portENTER_CRITICAL(&s_wc_route_mux);
  _active = this;
  portEXIT_CRITICAL(&s_wc_route_mux);
}

void WebConfigServer::detachRoutes() {
  portENTER_CRITICAL(&s_wc_route_mux);
  if (_active == this) _active = NULL;
  portEXIT_CRITICAL(&s_wc_route_mux);
}

uint32_t WebConfigServer::handlerRefCount() const {
  portENTER_CRITICAL(&s_wc_route_mux);
  uint32_t refs = _handler_refs;
  portEXIT_CRITICAL(&s_wc_route_mux);
  return refs;
}

void WebConfigServer::dispatchRequest(AsyncWebServerRequest* req, RequestHandler handler) {
  wcLogReq(req);
  WebConfigServer* target = NULL;
  portENTER_CRITICAL(&s_wc_route_mux);
  if (_active != NULL) {
    target = _active;
    target->_handler_refs++;
  }
  portEXIT_CRITICAL(&s_wc_route_mux);

  if (target == NULL) {
    req->send(503, "application/json", "{\"error\":\"webconfig stopped\"}");
    return;
  }

  (target->*handler)(req);

  portENTER_CRITICAL(&s_wc_route_mux);
  if (target->_handler_refs > 0) target->_handler_refs--;
  portEXIT_CRITICAL(&s_wc_route_mux);
}

void WebConfigServer::registerRoutes() {
  _server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleRoot); });
  _server->on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleStatus); });
  _server->on("/api/presets", HTTP_GET, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handlePresets); });
  _server->on("/api/login", HTTP_POST, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleLogin); },
              NULL, collectBody);
  _server->on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleLogout); });
  // NB: plain-string routes match sub-paths too ("/api/config" matches
  // "/api/config/result") and handlers run in registration order, so the more
  // specific route MUST be registered first or it never fires. This was why
  // save confirmations were lost: result polls were answered with config JSON.
  _server->on("/api/config/result", HTTP_GET, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleConfigResult); });
  _server->on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleConfigGet); });
  _server->on("/api/config", HTTP_POST, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleConfigPost); },
              NULL, collectBody);
  // Same specific-route-first rule as /api/config above.
  _server->on("/api/cli/result", HTTP_GET, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleCliResult); });
  _server->on("/api/cli", HTTP_POST, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleCliPost); },
              NULL, collectBody);
  _server->on("/api/stats", HTTP_GET, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleStats); });
  _server->on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleScan); });
  _server->on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleReboot); });
  _server->on("/api/portal/exit", HTTP_POST, [](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handlePortalExit); });
  _server->onNotFound([](AsyncWebServerRequest* r) { dispatchRequest(r, &WebConfigServer::handleNotFound); });
}

// Accumulate a small JSON body into request->_tempObject (freed automatically
// by the request destructor). Oversized bodies are left unbuffered and
// rejected in the completion handler.
void WebConfigServer::collectBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                                  size_t index, size_t total) {
  if (total == 0 || total > MAX_BODY) return;
  if (index == 0) {
    req->_tempObject = malloc(total + 1);
    if (req->_tempObject) ((char*)req->_tempObject)[total] = 0;
  }
  if (req->_tempObject) memcpy((uint8_t*)req->_tempObject + index, data, len);
}

bool WebConfigServer::checkAuth(AsyncWebServerRequest* req) {
  _last_activity = millis();
  if (_mode == MODE_SETUP) return true;  // physical proximity implied, nothing configured
  if (_mode != MODE_LAN) return false;
  if (_session_token[0] == 0) return false;
  if (!req->hasHeader("Cookie")) return false;
  const String& cookies = req->getHeader("Cookie")->value();
  int idx = cookies.indexOf("wcs=");
  if (idx < 0 || (int)cookies.length() < idx + 4 + 32) return false;
  String token = cookies.substring(idx + 4, idx + 4 + 32);
  uint32_t now = millis();
  if ((uint32_t)(now - _session_last_seen) > WEBCONFIG_SESSION_TTL_MS) return false;
  if (!fixedTimeEquals(token.c_str(), _session_token, 32)) return false;
  _session_last_seen = now;  // sliding expiry
  return true;
}

// ---------------------------------------------------------------------------
// Handlers (async_tcp task - no CLI/prefs writes, no radio access)
// ---------------------------------------------------------------------------

void WebConfigServer::handleRoot(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  _last_activity = millis();
  if (req->hasHeader("If-None-Match") &&
      req->getHeader("If-None-Match")->value() == WEBCONFIG_HTML_ETAG) {
    req->send(304);
    return;
  }
  AsyncWebServerResponse* res =
      req->beginResponse(200, "text/html", WEBCONFIG_HTML_GZ, WEBCONFIG_HTML_GZ_LEN);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("ETag", WEBCONFIG_HTML_ETAG);
  req->send(res);
}

void WebConfigServer::handleStatus(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  bool authed = checkAuth(req);

  DynamicJsonDocument doc(512);
  doc["mode"] = (_mode == MODE_SETUP) ? "setup" : "lan";
  doc["auth"] = authed;
  doc["needs_setup"] = (_obs->wifi_ssid[0] == 0);
  doc["name"] = (const char*)_prefs->node_name;
  char node_id[17];
  for (int i = 0; i < 8; i++) sprintf(&node_id[i * 2], "%02x", _pub_key[i]);
  doc["node_id"] = node_id;
  doc["fw"] = _fw_ver;
  // The page shows a trimmed version — base + build number + channel — and
  // pairs it with this, the way `ver` does. Both come from the same defines.
  doc["build_date"] = _build_date;
  doc["role"] = _role;
  doc["board"] = _board_name;
  doc["uptime_s"] = millis() / 1000;
  doc["runtime_slots"] = RUNTIME_MQTT_SLOTS;
  doc["max_slots"] = MAX_MQTT_SLOTS;
  // Servers the UI should expose: only as many as can actually be active at
  // once (2 without PSRAM, 5 with). Configuring more never connects.
  doc["active_slots"] = MQTTBridge::getMaxActiveSlots();
  // Commands the terminal may submit at once. The CLI shares the config
  // batch's fixed slot, so the cap is MAX_BATCH — reported rather than
  // duplicated in the page, which cannot know how this build was sized.
  doc["max_cmds"] = MAX_BATCH;

  AsyncResponseStream* res = req->beginResponseStream("application/json");
  serializeJson(doc, *res);
  req->send(res);
}

void WebConfigServer::handleLogin(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  _last_activity = millis();
  if (_mode == MODE_SETUP) {  // no auth in setup mode
    req->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  uint32_t now = millis();
  if (_login_lock_until && (int32_t)(now - _login_lock_until) < 0) {
    req->send(429, "application/json", "{\"error\":\"locked, retry in 30s\"}");
    return;
  }
  const char* body = (const char*)req->_tempObject;
  DynamicJsonDocument doc(256);
  if (!body || deserializeJson(doc, body) != DeserializationError::Ok) {
    req->send(400, "application/json", "{\"error\":\"bad request\"}");
    return;
  }
  const char* pwd = doc["password"] | "";
  if (!fixedTimeEquals(pwd, _prefs->password, sizeof(_prefs->password))) {
    if (++_login_fails >= 5) {
      _login_lock_until = now + 30000;
      if (_login_lock_until == 0) _login_lock_until = 1;
      _login_fails = 0;
    }
    req->send(401, "application/json", "{\"error\":\"wrong password\"}");
    return;
  }
  _login_fails = 0;
  _login_lock_until = 0;
  for (int i = 0; i < 4; i++) sprintf(&_session_token[i * 8], "%08lx", (unsigned long)esp_random());
  _session_last_seen = now;

  AsyncWebServerResponse* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
  char cookie[80];
  sprintf(cookie, "wcs=%s; HttpOnly; SameSite=Lax; Path=/", _session_token);
  res->addHeader("Set-Cookie", cookie);
  req->send(res);
}

void WebConfigServer::handleLogout(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  _session_token[0] = 0;
  AsyncWebServerResponse* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
  res->addHeader("Set-Cookie", "wcs=; Max-Age=0; Path=/");
  req->send(res);
}

void WebConfigServer::handleConfigGet(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"auth\"}"); return; }

  DynamicJsonDocument doc(6144);
  {
    WCLock lock(_mux);

    JsonObject radio = doc.createNestedObject("radio");
    // round via double so float error doesn't leak into the JSON
    // (910.525f would otherwise serialize as 910.5250244)
    radio["freq"] = (double)roundf(_prefs->freq * 1000.0f) / 1000.0;
    radio["bw"] = (double)roundf(_prefs->bw * 100.0f) / 100.0;
    radio["sf"] = _prefs->sf;
    radio["cr"] = _prefs->cr;
    radio["tx"] = _prefs->tx_power_dbm;
    radio["af"] = _prefs->airtime_factor;
    radio["rxdelay"] = _prefs->rx_delay_base;
    radio["txdelay"] = _prefs->tx_delay_factor;
    radio["cad"] = (bool)_prefs->cad_enabled;
    radio["rxgain"] = (bool)_prefs->rx_boosted_gain;
    radio["repeat"] = !(bool)_prefs->disable_fwd;   // CLI `repeat on` == disable_fwd 0
    radio["flood_max"] = _prefs->flood_max;
    radio["flood_max_advert"] = _prefs->flood_max_advert;
    radio["flood_max_unscoped"] = _prefs->flood_max_unscoped;
    static const char* const LOOP_MODES[] = { "off", "minimal", "moderate", "strict" };
    radio["loop_detect"] = LOOP_MODES[_prefs->loop_detect <= LOOP_DETECT_STRICT ? _prefs->loop_detect : 0];
    radio["name"] = (const char*)_prefs->node_name;
    radio["lat"] = _prefs->node_lat;
    radio["lon"] = _prefs->node_lon;
    radio["advert_interval"] = _prefs->advert_interval * 2;      // stored as mins/2
    radio["flood_advert_interval"] = _prefs->flood_advert_interval;  // hours

    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["ssid"] = (const char*)_obs->wifi_ssid;
    wifi["pwd"] = _obs->wifi_password[0] ? SECRET_SENTINEL : "";
    wifi["powersave"] = _obs->wifi_power_save == 0 ? "min"
                        : _obs->wifi_power_save == 2 ? "max" : "none";

    JsonObject mqtt = doc.createNestedObject("mqtt");
    mqtt["origin"] = (const char*)_obs->mqtt_origin;
    mqtt["iata"] = (const char*)_obs->mqtt_iata;
    mqtt["status"] = (bool)_obs->mqtt_status_enabled;
    mqtt["packets"] = (bool)_obs->mqtt_packets_enabled;
    mqtt["raw"] = (bool)_obs->mqtt_raw_enabled;
    mqtt["tx"] = _obs->mqtt_tx_enabled == 2 ? "advert"
                 : _obs->mqtt_tx_enabled == 1 ? "on" : "off";
    mqtt["rx"] = (bool)_obs->mqtt_rx_enabled;
    mqtt["interval"] = _obs->mqtt_status_interval / 60000;  // CLI takes minutes
    mqtt["neighbors"] = (bool)_obs->mqtt_neighbors_enabled;
    mqtt["neighbors_interval"] = _obs->mqtt_neighbors_interval / 3600000UL;  // CLI takes hours
    mqtt["timezone"] = (const char*)_obs->timezone_string;
    mqtt["timezone_offset"] = _obs->timezone_offset;
    mqtt["ntp"] = (const char*)_obs->mqtt_ntp_server;
    mqtt["owner"] = (const char*)_obs->mqtt_owner_public_key;
    mqtt["email"] = (const char*)_obs->mqtt_email;
    mqtt["snmp"] = (bool)_obs->snmp_enabled;
    mqtt["snmp_community"] = (const char*)_obs->snmp_community;

    JsonArray slots = mqtt.createNestedArray("slots");
    for (int i = 0; i < MAX_MQTT_SLOTS; i++) {
      JsonObject s = slots.createNestedObject();
      s["preset"] = (const char*)_obs->mqtt_slot_preset[i];
      s["server"] = (const char*)_obs->mqtt_slot_host[i];
      s["port"] = _obs->mqtt_slot_port[i];
      s["username"] = (const char*)_obs->mqtt_slot_username[i];
      s["password"] = _obs->mqtt_slot_password[i][0] ? SECRET_SENTINEL : "";
      s["token"] = _obs->mqtt_slot_token[i][0] ? SECRET_SENTINEL : "";
      s["topic"] = (const char*)_obs->mqtt_slot_topic[i];
      s["audience"] = (const char*)_obs->mqtt_slot_audience[i];
      char filter_text[MQTTPacketFilter::kFilterTextSize];
      if (MQTTPacketFilter::format(_obs->mqtt_slot_packet_filter[i],
                                   filter_text, sizeof(filter_text))) {
        // Mutable char input is copied into the ArduinoJson document; the
        // stack buffer is reused on the next slot.
        s["filter"] = filter_text;
      } else {
        s["filter"] = "all";
      }
    }
  }

  AsyncResponseStream* res = req->beginResponseStream("application/json");
  serializeJson(doc, *res);
  req->send(res);
}

void WebConfigServer::handleConfigPost(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"auth\"}"); return; }
  if (req->contentLength() > MAX_BODY) {
    req->send(413, "application/json", "{\"error\":\"body too large\"}");
    return;
  }
  const char* body = (const char*)req->_tempObject;
  DynamicJsonDocument doc(6144);
  if (!body || deserializeJson(doc, body) != DeserializationError::Ok) {
    req->send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  bool reboot_after = doc["reboot"] | false;
  const char* reqid = doc["reqid"] | "";
  if (!wcIsValidReqId(reqid)) {
    req->send(400, "application/json", "{\"error\":\"bad reqid\"}");
    return;
  }
  JsonObject set = doc["set"];

  WCLock lock(_mux);
  // A DONE batch stays readable until the next POST claims the slot, so a
  // client that lost the result response can re-poll instead of failing.
  // Repeating a POST with the same request ID is also idempotent: acknowledge
  // the batch already occupying the slot instead of applying its commands a
  // second time after an ambiguous network failure.
  // Classification lives in WebConfigBatch::classifyPost. It is consulted in two
  // phases because the change count is only known after the `set` map is parsed
  // below, and parsing must not run before Replay/Busy are answered (a replayed
  // POST carrying a bad key must still get its 202, not a 400). kCountUnknown is
  // a non-zero placeholder that keeps the count-dependent arms unreachable here.
  const WebConfigBatch::State bstate = toSpecState(_batch_state);
  const bool reqid_matches = (strcmp(reqid, _batch_reqid) == 0);
  const int kCountUnknown = 1;
  const WebConfigBatch::PostOutcome pre =
      WebConfigBatch::classifyPost(bstate, reqid_matches, kCountUnknown, reboot_after);

  if (pre == WebConfigBatch::PostOutcome::Replay) {
    StaticJsonDocument<96> ack;
    ack["state"] = WebConfigBatch::replayStateName(bstate);
    ack["count"] = _batch_count;
    ack["reqid"] = (const char*)_batch_reqid;
    String out;
    serializeJson(ack, out);
    req->send(202, "application/json", out);
    return;
  }
  if (pre == WebConfigBatch::PostOutcome::Busy) {
    // Echo the in-flight batch's reqid so the caller can tell its own retry
    // (same reqid — landed, keep polling) from another client's save.
    StaticJsonDocument<96> bd;
    bd["error"] = "busy";
    bd["reqid"] = (const char*)_batch_reqid;
    String out;
    serializeJson(bd, out);
    req->send(409, "application/json", out);
    return;
  }

  // First onboarding is not complete until the known factory password has
  // been replaced. Enforce this server-side so the Advanced editor or a crafted
  // request cannot save WiFi and strand the node with the default password.
  if (_mode == MODE_SETUP && _initial_setup && !set.containsKey("password") &&
      (reboot_after || set.containsKey("wifi.ssid"))) {
    req->send(400, "application/json", "{\"error\":\"admin password required for initial setup\"}");
    return;
  }

  int count = 0;
  for (JsonPair kv : set) {
    const char* key = kv.key().c_str();
    const char* val = kv.value().as<const char*>();
    // The admin password is the one key outside the `set` allowlist. It is safe
    // in both modes: MODE_OFF was refused above, MODE_LAN required a login to
    // get here, and MODE_SETUP has physical proximity. Rotating it is why the
    // portal exists — restricting it to the AP would force a bridge outage.
    const bool admin_pwd = wcIsAdminPasswordKey(key);
    if (!val || (!isAllowedSetKey(key) && !admin_pwd)) {
      // Build with ArduinoJson so an attacker-supplied key containing quotes or
      // backslashes is escaped rather than breaking out of the JSON string.
      char safe_key[33];
      strncpy(safe_key, key, sizeof(safe_key) - 1);
      safe_key[sizeof(safe_key) - 1] = 0;
      StaticJsonDocument<128> ed;
      ed["error"] = "bad key";
      ed["key"] = safe_key;
      String out;
      serializeJson(ed, out);
      req->send(400, "application/json", out);
      return;
    }
    if (admin_pwd && !wcIsValidAdminPassword(val)) {
      req->send(400, "application/json",
                "{\"error\":\"admin password must be 1-15 characters with no line breaks\"}");
      return;
    }
    if (isSecretKey(key) && strcmp(val, SECRET_SENTINEL) == 0) continue;  // unchanged
    if (count >= MAX_BATCH) {
      req->send(400, "application/json", "{\"error\":\"too many changes\"}");
      return;
    }
    BatchEntry& e = _batch[count];
    strncpy(e.key, key, sizeof(e.key) - 1);
    e.key[sizeof(e.key) - 1] = 0;
    // Build the allowlisted CLI command, stripping CR/LF from the value so it
    // can't smuggle in a second command. The admin password reuses the existing
    // top-level `password` command, so it persists exactly as the CLI does.
    int pos = admin_pwd ? snprintf(e.cmd, sizeof(e.cmd), "password ")
                        : snprintf(e.cmd, sizeof(e.cmd), "set %s ", key);
    bool overflow = false;
    for (const char* p = val; *p; p++) {
      if (*p == '\r' || *p == '\n') continue;
      if (pos >= (int)sizeof(e.cmd) - 1) { overflow = true; break; }
      e.cmd[pos++] = *p;
    }
    e.cmd[pos] = 0;
    // A value that doesn't fit used to be truncated here and applied anyway.
    // For length-checked keys the CLI would reject the remainder, but a key
    // whose grammar stays valid when clipped (mqttN.filter: "advert,2" cut to
    // "advert") would silently persist a *different* setting and still answer
    // OK. Refuse the batch instead — the caller can shorten and retry.
    if (overflow) {
      // Name the key, like the "bad key" rejection above: a 20-field batch is
      // rejected whole, so without it the operator has nothing to correct.
      char safe_key[33];
      strncpy(safe_key, key, sizeof(safe_key) - 1);
      safe_key[sizeof(safe_key) - 1] = 0;
      StaticJsonDocument<128> ed;
      ed["error"] = "value too long";
      ed["key"] = safe_key;
      String out;
      serializeJson(ed, out);
      req->send(400, "application/json", out);
      return;
    }
    count++;
  }
  // Phase 2: the count is now known, so the remaining NoChanges/Accept arms
  // resolve. Replay/Busy were already answered above.
  if (WebConfigBatch::classifyPost(bstate, reqid_matches, count, reboot_after) ==
      WebConfigBatch::PostOutcome::NoChanges) {
    req->send(400, "application/json", "{\"error\":\"no changes\"}");
    return;
  }
  _batch_kind = BATCH_CONFIG;
  _batch_count = count;
  _batch_next = 0;
  _batch_reboot = reboot_after;
  _batch_reboot_armed = false;
  _batch_all_ok = true;
  strncpy(_batch_reqid, reqid, sizeof(_batch_reqid) - 1);
  _batch_reqid[sizeof(_batch_reqid) - 1] = 0;
  _batch_state = BATCH_PENDING;  // tick() picks it up on the loop task
  uint32_t du = millis() + 60000;
  if (du == 0) du = 1;
  _diag_until = du;
  Serial.printf("WC: config POST accepted, %d cmds, reboot=%d\n", count, (int)reboot_after);

  StaticJsonDocument<96> ack;
  ack["state"] = "pending";
  ack["count"] = count;
  ack["reqid"] = (const char*)_batch_reqid;
  String out;
  serializeJson(ack, out);
  req->send(202, "application/json", out);
}

void WebConfigServer::handleConfigResult(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { Serial.println("WC: result read -> 503 (mode off)"); req->send(503); return; }
  if (!checkAuth(req)) { Serial.println("WC: result read -> 401"); req->send(401, "application/json", "{\"error\":\"auth\"}"); return; }
  if (!req->hasParam("reqid")) {
    req->send(400, "application/json", "{\"error\":\"bad reqid\"}");
    return;
  }
  String requested_reqid = req->getParam("reqid")->value();
  if (!wcIsValidReqId(requested_reqid.c_str())) {
    req->send(400, "application/json", "{\"error\":\"bad reqid\"}");
    return;
  }

  // Entry print BEFORE the lock (racy state read is fine for diag): if this
  // fires but no branch print follows, the handler is blocked on _mux.
  Serial.printf("WC: result entry mode=%d state=%d\n", (int)_mode, (int)_batch_state);
  WCLock lock(_mux);
  // A CLI sequence occupying the shared slot is not a config save, whatever the
  // reqid says: its entries have no `key` and its results belong to the
  // terminal's reader. Treat it as unknown here (and vice versa there).
  const bool mine = (_batch_kind == BATCH_CONFIG) &&
                    (strcmp(requested_reqid.c_str(), _batch_reqid) == 0);
  const WebConfigBatch::ResultOutcome outcome =
      WebConfigBatch::classifyResult(toSpecState(_batch_state), mine);
  if (outcome == WebConfigBatch::ResultOutcome::Idle) {
    Serial.println("WC: result read -> idle");
    StaticJsonDocument<64> idle;
    idle["state"] = "idle";
    idle["reqid"] = requested_reqid;
    String out;
    serializeJson(idle, out);
    req->send(200, "application/json", out);
    return;
  }
  if (outcome == WebConfigBatch::ResultOutcome::Unknown) {
    req->send(404, "application/json", "{\"error\":\"unknown request\"}");
    return;
  }
  if (outcome == WebConfigBatch::ResultOutcome::Pending) {
    StaticJsonDocument<96> pd;
    pd["state"] = "pending";
    pd["reqid"] = (const char*)_batch_reqid;
    String out;
    serializeJson(pd, out);
    req->send(200, "application/json", out);
    return;
  }
  Serial.printf("WC: result read -> done (reboot=%d armed=%d all_ok=%d)\n",
                (int)_batch_reboot, (int)_batch_reboot_armed, (int)_batch_all_ok);
  DynamicJsonDocument doc(6144);
  doc["state"] = "done";
  // Only advertise a reboot when it will actually happen: a partially-failed
  // batch is not rebooted (see below), so the UI must not show a reboot screen.
  doc["reboot"] = WebConfigBatch::doneReportsReboot(_batch_reboot, _batch_all_ok);
  doc["all_ok"] = _batch_all_ok;
  doc["reqid"] = (const char*)_batch_reqid;
  JsonArray results = doc.createNestedArray("results");
  for (int i = 0; i < _batch_count; i++) {
    JsonObject r = results.createNestedObject();
    r["key"] = (const char*)_batch[i].key;
    r["reply"] = (const char*)_batch[i].reply;
  }
  // State stays DONE (re-readable) until the next POST claims the slot.
  if (WebConfigBatch::shouldArmConfirmReboot(toSpecState(_batch_state), _batch_reboot,
                                             _batch_all_ok, _batch_reboot_armed)) {
    // Confirmation delivered and every command succeeded — reboot 3 s from now
    // (replaces the 30 s drain-time fallback) so the UI can show its countdown
    // first. Armed once; re-reads must not keep pushing the deadline out. A
    // partially-failed batch is deliberately left running so the operator can
    // correct and retry instead of rebooting into a broken config.
    _batch_reboot_armed = true;
    _reboot_at = WebConfigBatch::confirmRebootAt(millis());
  }

  AsyncResponseStream* res = req->beginResponseStream("application/json");
  serializeJson(doc, *res);
  req->send(res);
}

// ---------------------------------------------------------------------------
// CLI terminal (/api/cli). Same 202 + reqid + poll contract as a config save,
// and for the same reason: CommonCLI touches prefs, the radio and the
// filesystem, none of which may be reached from the async_tcp task. The
// commands go into the shared deferred slot and tick() drains them.
//
// Unlike a save this is NOT allowlisted. That is the point: the terminal exists
// to reach what the serial console reaches, and execCommand() passes
// sender_timestamp 0, so it gets the same local privilege the serial console
// has. Authentication is the boundary — as it is for serial (physical access)
// and for remote admin over the mesh (the admin password).
// ---------------------------------------------------------------------------

// Commands whose CLI handler never returns would take the node down mid-drain,
// before the client could read a single result. `reboot` is deferred instead:
// it is not passed to the CLI at all, and the batch arms the ordinary reboot
// path once the operator has read the results. The rest (clkreboot, poweroff,
// ota update) do real work on the way down and cannot be faked, so they run
// normally and the connection drops — the UI warns before sending them.
void WebConfigServer::handleCliPost(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"auth\"}"); return; }
  if (req->_tempObject == NULL) {
    req->send(413, "application/json", "{\"error\":\"body too large\"}");
    return;
  }
  const char* body = (const char*)req->_tempObject;
  DynamicJsonDocument doc(6144);
  if (!body || deserializeJson(doc, body) != DeserializationError::Ok) {
    req->send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  const char* reqid = doc["reqid"] | "";
  if (!wcIsValidReqId(reqid)) {
    req->send(400, "application/json", "{\"error\":\"bad reqid\"}");
    return;
  }
  JsonArray cmds = doc["cmds"];
  if (cmds.isNull()) {
    req->send(400, "application/json", "{\"error\":\"no commands\"}");
    return;
  }

  WCLock lock(_mux);
  // Replay/Busy exactly as a config save classifies them: a repeated POST is
  // acknowledged rather than executed twice, and a different sequence while one
  // is still draining is refused.
  const WebConfigBatch::State bstate = toSpecState(_batch_state);
  const bool reqid_matches = (strcmp(reqid, _batch_reqid) == 0);
  const WebConfigBatch::PostOutcome pre =
      WebConfigBatch::classifyPost(bstate, reqid_matches, 1 /* count unknown yet */, false);
  if (pre == WebConfigBatch::PostOutcome::Replay) {
    StaticJsonDocument<96> ack;
    ack["state"] = (bstate == WebConfigBatch::State::Done) ? "done" : "running";
    ack["total"] = _batch_count;
    ack["reqid"] = (const char*)_batch_reqid;
    String out;
    serializeJson(ack, out);
    req->send(202, "application/json", out);
    return;
  }
  if (pre == WebConfigBatch::PostOutcome::Busy) {
    StaticJsonDocument<96> bd;
    bd["error"] = "busy";
    bd["reqid"] = (const char*)_batch_reqid;
    String out;
    serializeJson(bd, out);
    req->send(409, "application/json", out);
    return;
  }

  int count = 0;
  bool defer_reboot = false, seq_sets_pwd = false, seq_sets_ssid = false;
  for (JsonVariant v : cmds) {
    const char* raw = v.as<const char*>();
    if (!raw) continue;
    if (count >= MAX_BATCH) {
      StaticJsonDocument<96> ed;
      ed["error"] = "too many commands";
      ed["max"] = MAX_BATCH;
      String out;
      serializeJson(ed, out);
      req->send(413, "application/json", out);
      return;
    }
    // Strip CR/LF so one entry cannot smuggle a second command past the
    // operator's confirmation, and skip whatever is left blank.
    BatchEntry& e = _batch[count];
    int pos = 0;
    for (const char* p = raw; *p; p++) {
      if (*p == '\r' || *p == '\n') continue;
      if (pos == 0 && (*p == ' ' || *p == '\t')) continue;   // leading space
      if (pos >= (int)sizeof(e.cmd) - 1) {
        req->send(400, "application/json", "{\"error\":\"command too long\"}");
        return;
      }
      e.cmd[pos++] = *p;
    }
    while (pos > 0 && (e.cmd[pos - 1] == ' ' || e.cmd[pos - 1] == '\t')) pos--;
    e.cmd[pos] = 0;
    if (pos == 0) continue;
    // Reject before anything runs, so a sequence never half-applies and then
    // stops on a command that was never going to work here.
    const char* why = wcCliUnavailable(e.cmd);
    if (why) {
      StaticJsonDocument<256> ed;
      ed["error"] = why;
      String out;
      serializeJson(ed, out);
      req->send(400, "application/json", out);
      return;
    }
    e.key[0] = 0;                       // CLI entries have no config key
    if (wcIsDeferredReboot(e.cmd)) defer_reboot = true;
    if (strncmp(e.cmd, "password ", 9) == 0) seq_sets_pwd = true;
    if (strncmp(e.cmd, "set wifi.ssid ", 14) == 0) seq_sets_ssid = true;
    count++;
  }
  if (count == 0) {
    req->send(400, "application/json", "{\"error\":\"no commands\"}");
    return;
  }

  // The same invariant handleConfigPost enforces, and for the same reason: the
  // reboot is what commits first onboarding, and a node that reboots onto the
  // LAN still holding the factory password is a known credential on someone
  // else's network. The terminal warned about this client-side, which is a
  // reminder, not a rule — a pasted script or a direct POST ignored it.
  if (_mode == MODE_SETUP && _initial_setup && !seq_sets_pwd && !_admin_pwd_set &&
      (defer_reboot || seq_sets_ssid)) {
    req->send(400, "application/json",
              "{\"error\":\"admin password required for initial setup — "
              "run `password <new-password>` first\"}");
    return;
  }

  _batch_kind = BATCH_CLI;
  _batch_count = count;
  _batch_next = 0;
  _batch_reboot = defer_reboot;
  _batch_reboot_armed = false;
  _batch_all_ok = true;
  strncpy(_batch_reqid, reqid, sizeof(_batch_reqid) - 1);
  _batch_reqid[sizeof(_batch_reqid) - 1] = 0;
  _batch_state = BATCH_PENDING;         // tick() picks it up on the loop task
  Serial.printf("WC: cli POST accepted, %d cmds, reboot=%d\n", count, (int)defer_reboot);

  StaticJsonDocument<96> ack;
  ack["state"] = "running";
  ack["total"] = count;
  ack["reqid"] = (const char*)_batch_reqid;
  String out;
  serializeJson(ack, out);
  req->send(202, "application/json", out);
}

void WebConfigServer::handleCliResult(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"auth\"}"); return; }
  if (!req->hasParam("reqid")) {
    req->send(400, "application/json", "{\"error\":\"bad reqid\"}");
    return;
  }
  String requested_reqid = req->getParam("reqid")->value();
  if (!wcIsValidReqId(requested_reqid.c_str())) {
    req->send(400, "application/json", "{\"error\":\"bad reqid\"}");
    return;
  }
  int from = 0;
  if (req->hasParam("from")) {
    from = req->getParam("from")->value().toInt();
    if (from < 0) from = 0;
  }

  WCLock lock(_mux);
  // A config save occupying the slot is not this client's sequence, whatever
  // the reqid says; treat it as unknown rather than serving `set` results
  // through the terminal's reader.
  const bool mine = (_batch_kind == BATCH_CLI) &&
                    (strcmp(requested_reqid.c_str(), _batch_reqid) == 0);
  const WebConfigBatch::ResultOutcome outcome =
      WebConfigBatch::classifyResult(toSpecState(_batch_state), mine);
  if (outcome == WebConfigBatch::ResultOutcome::Idle) {
    StaticJsonDocument<64> idle;
    idle["state"] = "idle";
    idle["reqid"] = requested_reqid;
    String out;
    serializeJson(idle, out);
    req->send(200, "application/json", out);
    return;
  }
  if (outcome == WebConfigBatch::ResultOutcome::Unknown) {
    req->send(404, "application/json", "{\"error\":\"unknown request\"}");
    return;
  }

  // Results stream: hand back whatever has drained since the client's cursor,
  // capped so the document stays small on the async_tcp task.
  const int produced = _batch_next;
  const int page = WebConfigBatch::cliPageCount(from, produced, WebConfigBatch::kCliResultPage);
  const bool final_read = WebConfigBatch::cliReadIsFinal(toSpecState(_batch_state),
                                                         from, page, _batch_count);
  DynamicJsonDocument doc(4096);
  doc["state"] = final_read ? "done" : "running";
  doc["reqid"] = (const char*)_batch_reqid;
  doc["total"] = _batch_count;
  doc["from"] = from;
  JsonArray results = doc.createNestedArray("results");
  for (int i = from; i < from + page; i++) {
    JsonObject r = results.createNestedObject();
    // The command is deliberately NOT echoed: it may hold a password or token,
    // and the client already has the sequence it sent. It matches by index.
    r["ok"] = !WebConfigBatch::cliReplyIsFailure(_batch[i].reply);
    r["reply"] = (const char*)_batch[i].reply;
  }
  if (final_read) {
    doc["all_ok"] = _batch_all_ok;
    const bool rebooting = WebConfigBatch::cliRebootAllowed(_batch_reboot, _batch_all_ok);
    doc["reboot"] = rebooting;
    // Tell the operator why a `reboot` they asked for is not happening.
    if (_batch_reboot && !_batch_all_ok) doc["reboot_withheld"] = true;
    if (WebConfigBatch::shouldArmConfirmReboot(toSpecState(_batch_state), _batch_reboot,
                                               _batch_all_ok, _batch_reboot_armed)) {
      _batch_reboot_armed = true;
      _reboot_at = WebConfigBatch::confirmRebootAt(millis());
    }
  }
  AsyncResponseStream* res = req->beginResponseStream("application/json");
  serializeJson(doc, *res);
  req->send(res);
}

void WebConfigServer::handleStats(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"auth\"}"); return; }
  uint32_t until = millis() + 15000;
  if (until == 0) until = 1;
  _stats_wanted_until = until;  // tick() refreshes the snapshot while polled

  WCLock lock(_mux);
  if (_stats_json[0] == 0) {
    req->send(200, "application/json", "{\"state\":\"pending\"}");
    return;
  }
  req->send(200, "application/json", _stats_json);
}

void WebConfigServer::handleScan(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"auth\"}"); return; }

  int n = WiFi.scanComplete();
  if (req->hasParam("rescan") && n >= 0) {
    WiFi.scanDelete();
    n = WIFI_SCAN_FAILED;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    req->send(200, "application/json", "{\"state\":\"scanning\"}");
    return;
  }
  if (n < 0) {  // WIFI_SCAN_RUNNING
    req->send(200, "application/json", "{\"state\":\"scanning\"}");
    return;
  }
  DynamicJsonDocument doc(3072);
  doc["state"] = "done";
  JsonArray nets = doc.createNestedArray("networks");
  for (int i = 0; i < n && i < 20; i++) {
    JsonObject net = nets.createNestedObject();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["enc"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  AsyncResponseStream* res = req->beginResponseStream("application/json");
  serializeJson(doc, *res);
  req->send(res);
}

void WebConfigServer::handlePresets(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  _last_activity = millis();
  DynamicJsonDocument doc(3072);
  JsonArray arr = doc.createNestedArray("presets");
  for (int i = 0; i < MQTT_PRESET_COUNT; i++) {
    const MQTTPresetDef& p = MQTT_PRESETS[i];
    JsonObject o = arr.createNestedObject();
    o["name"] = p.name;
    // What the UI must collect for this preset to connect
    if (p.topic_style == MQTT_TOPIC_MESHRANK) {
      o["needs"] = "token";
    } else if (mqttPresetNeedsSlotUsername(&p) && mqttPresetNeedsSlotPassword(&p)) {
      o["needs"] = "userpass";
    } else if (mqttPresetNeedsSlotPassword(&p)) {
      o["needs"] = "password";
    } else if (mqttPresetNeedsSlotUsername(&p)) {
      o["needs"] = "userpass";
    } else {
      o["needs"] = "none";
    }
  }
  AsyncResponseStream* res = req->beginResponseStream("application/json");
  serializeJson(doc, *res);
  req->send(res);
}

void WebConfigServer::handleReboot(AsyncWebServerRequest* req) {
  if (_mode == MODE_OFF) { req->send(503); return; }
  if (!checkAuth(req)) { req->send(401, "application/json", "{\"error\":\"auth\"}"); return; }
  _reboot_at = millis() + 1500;
  if (_reboot_at == 0) _reboot_at = 1;
  req->send(200, "application/json", "{\"ok\":true}");
}

void WebConfigServer::handlePortalExit(AsyncWebServerRequest* req) {
  // Setup mode only: switch captive probes to native "success" replies so the
  // OS sign-in sheet can be dismissed (iOS: "Done") without dropping the WiFi;
  // the user then continues at http://<softAP IP>/ in their real browser,
  // which survives the phone sleeping (the captive sheet does not).
  if (_mode != MODE_SETUP) { req->send(404); return; }
  _captive_release = true;
  _last_activity = millis();
  char body[64];
  snprintf(body, sizeof(body), "{\"ok\":true,\"url\":\"http://%s/\"}",
           WiFi.softAPIP().toString().c_str());
  req->send(200, "application/json", body);
}

void WebConfigServer::handleNotFound(AsyncWebServerRequest* req) {
  // Captive-portal probes (/generate_204, /hotspot-detect.html, /ncsi.txt,
  // /connecttest.txt, ...) all land here; a redirect to the portal makes the
  // phone pop its sign-in sheet.
  if (_mode == MODE_SETUP && req->method() == HTTP_GET) {
    if (_captive_release) {
      // Answer each OS's connectivity check natively so the sheet reports
      // success and can be closed. Deliberately does NOT bump _last_activity:
      // background probes must not hold the portal open past the idle timeout.
      const String& url = req->url();
      if (url.indexOf("generate_204") >= 0 || url.indexOf("gen_204") >= 0) {
        req->send(204);                                       // Android
      } else if (url.indexOf("hotspot-detect") >= 0 || url.indexOf("success") >= 0) {
        req->send(200, "text/html",                            // Apple CNA
                  "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
      } else if (url.indexOf("ncsi.txt") >= 0) {
        req->send(200, "text/plain", "Microsoft NCSI");        // Windows
      } else if (url.indexOf("connecttest.txt") >= 0) {
        req->send(200, "text/plain", "Microsoft Connect Test");
      } else {
        req->send(404);
      }
      return;
    }
    _last_activity = millis();
    req->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
    return;
  }
  req->send(404);
}

#endif  // ESP_PLATFORM && WITH_MQTT_BRIDGE
