#include "MQTTBridge.h"
#include "../MQTTConnectionPolicy.h"
#include "../MQTTMessageBuilder.h"
#include "../MQTTPacketQueuePolicy.h"
#include "../MQTTReplyFormat.h"
#include "../MQTTRuntimeBufferLifecycle.h"
#include "../MQTTTopicRouter.h"
#include "../TxtDataHelpers.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <new>
#include <strings.h>

#ifdef WITH_SNMP
#include "../SNMPAgent.h"
#endif

#ifdef ESP_PLATFORM
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <mbedtls/platform.h>
#endif

// Effective MQTT origin: empty mqtt_origin follows node_name; otherwise mqtt_origin override (quotes stripped).
static void applyEffectiveOrigin(const NodePrefs* np, const MQTTPrefs* obs, char* dest, size_t dest_size) {
  if (!np || !obs || !dest || dest_size == 0) return;
  if (obs->mqtt_origin[0] == '\0') {
    strncpy(dest, np->node_name, dest_size - 1);
  } else {
    strncpy(dest, obs->mqtt_origin, dest_size - 1);
  }
  dest[dest_size - 1] = '\0';
  StrHelper::stripSurroundingQuotes(dest, dest_size);
}

static const char* const kNtpBuiltinFallbacks[] = {
  "pool.ntp.org",
  "time.google.com",
  "time.cloudflare.com",
  "time.aws.com",
  "time.nist.gov",
};
static constexpr size_t kNtpBuiltinFallbackCount =
    sizeof(kNtpBuiltinFallbacks) / sizeof(kNtpBuiltinFallbacks[0]);
static_assert(MQTTBridge::kMaxNtpServers >= 1 + (int)kNtpBuiltinFallbackCount,
              "kMaxNtpServers must hold the custom primary plus all built-in fallbacks");

static bool ntpHostnameEquals(const char* a, const char* b) {
  if (!a || !b) return false;
  return strcasecmp(a, b) == 0;
}

static void fillNtpServerList(const MQTTPrefs* prefs, const char* servers[], int& count) {
  count = 0;
  if (prefs && prefs->mqtt_ntp_server[0] != '\0') {
    servers[count++] = prefs->mqtt_ntp_server;
  }
  for (size_t i = 0; i < kNtpBuiltinFallbackCount && count < MQTTBridge::kMaxNtpServers; i++) {
    const char* fb = kNtpBuiltinFallbacks[i];
    bool dup = false;
    for (int j = 0; j < count; j++) {
      if (ntpHostnameEquals(servers[j], fb)) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      servers[count++] = fb;
    }
  }
}

const char* MQTTBridge::effectiveNtpPrimary(const MQTTPrefs* obs) {
  if (obs && obs->mqtt_ntp_server[0] != '\0') {
    return obs->mqtt_ntp_server;
  }
  return kNtpBuiltinFallbacks[0];
}

void MQTTBridge::refreshOriginFromPrefs() {
  if (!_prefs) return;
  applyEffectiveOrigin(_prefs, _obs, _origin, sizeof(_origin));
}

void MQTTBridge::getEffectiveMqttOrigin(const NodePrefs* np, const MQTTPrefs* obs, char* buf, size_t buf_size) {
  if (!buf || buf_size == 0) return;
  if (!np || !obs) {
    buf[0] = '\0';
    return;
  }
  applyEffectiveOrigin(np, obs, buf, buf_size);
}

// Helper function to check if WiFi credentials are valid
static bool isWiFiConfigValid(const MQTTPrefs* obs) {
  // Check if WiFi SSID is configured (not empty)
  if (!obs || strlen(obs->wifi_ssid) == 0) {
    return false;
  }

  // WiFi password can be empty for open networks, so we don't check it

  return true;
}

#ifdef WITH_MQTT_BRIDGE

// A custom slot endpoint is complete if a port is set, or if the host is a
// full URI with a scheme (esp-mqtt applies scheme default ports, and the URI
// builder in setupSlot() preserves any embedded port/path).
static bool customEndpointComplete(const char* host, uint16_t port) {
  return host[0] != '\0' && (port != 0 || strstr(host, "://") != nullptr);
}

bool MQTTBridge::isConfigValid(const MQTTPrefs* obs) {
  if (!obs || !isWiFiConfigValid(obs)) return false;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    const char* preset_name = obs->mqtt_slot_preset[i];
    if (preset_name[0] == '\0' || strcmp(preset_name, MQTT_PRESET_NONE) == 0) continue;
    if (strcmp(preset_name, MQTT_PRESET_CUSTOM) == 0) {
      if (customEndpointComplete(obs->mqtt_slot_host[i], obs->mqtt_slot_port[i])) return true;
    } else if (findMQTTPreset(preset_name) != nullptr) {
      return true;
    }
  }
  return false;
}

// Optional embedded CA bundle symbols produced by board_build.embed_files.
// Weak linkage keeps non-bundle builds linkable and allows runtime fallback.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_src_certs_x509_crt_bundle_bin_start") __attribute__((weak));
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_src_certs_x509_crt_bundle_bin_end") __attribute__((weak));

// Track whether the global cert bundle has been loaded into s_crt_bundle.
// Loading must happen exactly once to avoid a use-after-free race when multiple
// TLS slots are set up in sequence (each connect() launches an async task).
static bool s_ca_bundle_loaded = false;

// PSRAM-aware allocation: prefer PSRAM on ESP32 when BOARD_HAS_PSRAM, fallback to internal heap or malloc.
// Use psram_free() for any pointer returned by psram_malloc().
static void* psram_malloc(size_t size) {
  if (size == 0) return nullptr;
#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (p != nullptr) return p;
  p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
  return p;
#else
  return malloc(size);
#endif
}

static void* psram_calloc(size_t n, size_t size) {
  if (n == 0 || size == 0) return nullptr;
#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
  if (p != nullptr) return p;
  return heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL);
#else
  return calloc(n, size);
#endif
}

static void psram_free(void* ptr) {
  if (ptr == nullptr) return;
#if defined(ESP_PLATFORM)
  heap_caps_free(ptr);
#else
  free(ptr);
#endif
}

static void* psram_realloc(void* ptr, size_t new_size) {
  if (new_size == 0) {
    psram_free(ptr);
    return nullptr;
  }
#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
  void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
  if (p != nullptr) return p;
  // A block that fell back to internal DRAM on allocation (PSRAM exhausted) cannot
  // be grown in PSRAM; retry there rather than reporting failure.
  return heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL);
#else
  return realloc(ptr, new_size);
#endif
}

// Shared JSON document pools follow the same PSRAM-first policy as the bridge's
// text buffers. ArduinoJson calls reallocate() when shrinking its pool list and
// asserts the result is non-null for a shrink, which both branches above satisfy.
void* MQTTBridge::JsonScratchAllocator::allocate(size_t size) {
  return psram_malloc(size);
}

void MQTTBridge::JsonScratchAllocator::deallocate(void* ptr) {
  psram_free(ptr);
}

void* MQTTBridge::JsonScratchAllocator::reallocate(void* ptr, size_t new_size) {
  return psram_realloc(ptr, new_size);
}

// Time (millis()) when WiFi was last seen connected; 0 when disconnected. Used for get wifi.status uptime.
static unsigned long s_wifi_connected_at = 0;

// Last WiFi disconnect reason (from ESP-IDF event). Used for get wifi.status diagnostics.
static uint8_t s_wifi_disconnect_reason = 0;
static unsigned long s_wifi_disconnect_time = 0;

#ifdef MQTT_MEMORY_DEBUG
// #region agent log
static void agentLogHeap(const char* location, const char* message, const char* hypothesisId,
                         size_t free_h, size_t max_alloc, unsigned long internal_free, unsigned long spiram_free) {
  char buf[320];
  snprintf(buf, sizeof(buf),
          "{\"sessionId\":\"debug-session\",\"location\":\"%s\",\"message\":\"%s\",\"hypothesisId\":\"%s\","
          "\"data\":{\"free\":%u,\"max_alloc\":%u,\"internal_free\":%lu,\"spiram_free\":%lu},\"timestamp\":%lu}",
          location, message, hypothesisId, (unsigned)free_h, (unsigned)max_alloc, internal_free, spiram_free,
          (unsigned long)millis());
  Serial.println(buf);
}
// #endregion
#endif

// Singleton for formatMqttStatusReply (set in begin(), cleared in end())
static MQTTBridge* s_mqtt_bridge_instance = nullptr;

unsigned long MQTTBridge::getWifiConnectedAtMillis() {
  return s_wifi_connected_at;
}

#if defined(WITH_MQTT_NEIGHBORS)
// Compact "time remaining" for the `get mqtt.status` nbr field: "3h12m" / "12m" / "45s".
static void formatDuration(char* buf, size_t len, uint32_t secs) {
  if (!buf || len == 0) return;
  uint32_t h = secs / 3600;
  uint32_t m = (secs % 3600) / 60;
  if (h > 0) {
    snprintf(buf, len, "%uh%um", (unsigned)h, (unsigned)m);
  } else if (m > 0) {
    snprintf(buf, len, "%um", (unsigned)m);
  } else {
    snprintf(buf, len, "%us", (unsigned)secs);
  }
}
#endif

void MQTTBridge::formatMqttStatusReply(char* buf, size_t bufsize, const MQTTPrefs* obs) {
  if (buf == nullptr || bufsize == 0) return;
  const char* msgs = (obs && obs->mqtt_status_enabled) ? "on" : "off";
  if (s_mqtt_bridge_instance == nullptr || !s_mqtt_bridge_instance->_initialized) {
    snprintf(buf, bufsize, "> msgs: %s (bridge not running)", msgs);
    return;
  }
  MQTTBridge* b = s_mqtt_bridge_instance;

  // Build per-slot status strings (compact format to fit 160-byte reply buffer)
  // Only show configured slots, skip "none" slots
  int q = 0;
#ifdef ESP_PLATFORM
  if (b->_packet_queue_handle != nullptr) {
    q = (int)uxQueueMessagesWaiting(b->_packet_queue_handle);
  }
#else
  q = b->_queue_count;
#endif

  // replyAppendf clamps pos into the buffer on every call, so no per-append
  // guard or trailing clamp is needed (see MQTTReplyFormat.h / A1).
  int pos = 0;
  replyAppendf(buf, bufsize, &pos, "> msgs: %s", msgs);
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    const MQTTSlot& slot = b->_slots[i];
    const char* name = nullptr;
    const char* state = nullptr;

    if (!slot.enabled && slot.preset) {
      name = slot.preset->name;
      state = "inactive";
    } else if (!slot.enabled) {
      continue;  // Skip unconfigured slots
    } else if (!b->isSlotReady(i)) {
      name = slot.preset ? slot.preset->name : "custom";
      state = "wait";
    } else if (slot.connected) {
      name = slot.preset ? slot.preset->name : "custom";
      state = "ok";
    } else if (slot.circuit_breaker_tripped) {
      name = slot.preset ? slot.preset->name : "custom";
      state = "fail";
    } else {
      name = slot.preset ? slot.preset->name : "custom";
      state = "disc";
    }
    replyAppendf(buf, bufsize, &pos, ", %d: %s (%s)", i + 1, name, state);
  }
  replyAppendf(buf, bufsize, &pos, ", q:%d", q);

#if defined(WITH_MQTT_NEIGHBORS)
  // Periodic neighbors: time to next publish + how the last one went.
  if (obs && obs->mqtt_neighbors_enabled) {
    char when[16];
    switch (b->_neighbors_phase.load(std::memory_order_relaxed)) {
      case NBR_ACTIVE: strcpy(when, "active"); break;
      case NBR_DUE:    strcpy(when, "due"); break;
      default:
        formatDuration(when, sizeof(when),
                       b->_neighbors_secs_until_next.load(std::memory_order_relaxed));
        break;
    }
    const char* last;
    switch (b->_neighbors_last_result.load(std::memory_order_relaxed)) {
      case NBR_RESULT_OK:   last = "ok"; break;
      case NBR_RESULT_FAIL: last = "failed"; break;
      default:              last = "none"; break;
    }
    replyAppendf(buf, bufsize, &pos, ", nbr: %s/%s", when, last);
  }
#endif
}

// On-demand publish-health + heap snapshot for the `get mqtt.stats` CLI command.
// Same data as the (MQTT_MEMORY_DEBUG-only) periodic logMemoryStatus() line, but
// returned as a reply instead of logged. Per-slot "sN=ok/err": ok = cumulative
// accepted publishes, err = cumulative failures (socket error / network timeout).
// Outbox should read ~0 (QoS0 publishes synchronously); a rising err isolates a
// broker whose uplink is dropping writes.
void MQTTBridge::formatMqttStatsReply(char* buf, size_t bufsize) {
  if (buf == nullptr || bufsize == 0) return;
  if (s_mqtt_bridge_instance == nullptr || !s_mqtt_bridge_instance->_initialized) {
    snprintf(buf, bufsize, "> (bridge not running)");
    return;
  }
  MQTTBridge* b = s_mqtt_bridge_instance;

  int q = 0;
#ifdef ESP_PLATFORM
  if (b->_packet_queue_handle != nullptr) {
    q = (int)uxQueueMessagesWaiting(b->_packet_queue_handle);
  }
#else
  q = b->_queue_count;
#endif

  size_t outbox_total = 0;
  unsigned long outbox_drops = 0;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (b->_slots[i].client) {
      outbox_total += b->_slots[i].client->getOutboxSize();
      outbox_drops += b->_slots[i].client->getOutboxDrops();
    }
  }

  // drops=<outbox>/<skipped>: outbox-cap drops vs. memory-pressure skips.
  int pos = 0;
  replyAppendf(buf, bufsize, &pos, "> Free=%d Max=%d q:%d/%d Outbox=%u drops=%lu/%d",
               (int)ESP.getFreeHeap(), (int)ESP.getMaxAllocHeap(),
               q, MAX_QUEUE_SIZE, (unsigned)outbox_total,
               outbox_drops, b->_skipped_publishes);
  // filt=<n>: packets the per-slot type filters rejected before the queue.
  // Omitted while zero so an unfiltered node's reply keeps its former length —
  // the per-slot list below is what usually gets clamped away first.
  if (b->_filtered_packets > 0) {
    replyAppendf(buf, bufsize, &pos, " filt=%lu", b->_filtered_packets);
  }
  replyAppendf(buf, bufsize, &pos, " |");
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (!b->_slots[i].enabled || !b->_slots[i].client) continue;
    replyAppendf(buf, bufsize, &pos, " s%d=%lu/%lu", i + 1,
                 b->_slots[i].client->getPublishOk(),
                 b->_slots[i].client->getPublishErr());
  }
}

// Structured per-slot status for the webconfig stats endpoint. Same state
// derivation as formatMqttStatusReply above. Returns false for out-of-range,
// unconfigured, or bridge-not-running slots.
bool MQTTBridge::getSlotStatusSnapshot(int slot_index, SlotStatusSnapshot* out) {
  if (out == nullptr || slot_index < 0 || slot_index >= RUNTIME_MQTT_SLOTS) return false;
  if (s_mqtt_bridge_instance == nullptr || !s_mqtt_bridge_instance->_initialized) return false;
  MQTTBridge* b = s_mqtt_bridge_instance;
  const MQTTSlot& slot = b->_slots[slot_index];

  if (!slot.enabled && slot.preset) {
    out->name = slot.preset->name;
    out->state = "inactive";
  } else if (!slot.enabled) {
    return false;  // unconfigured slot
  } else {
    out->name = slot.preset ? slot.preset->name : "custom";
    if (!b->isSlotReady(slot_index)) {
      out->state = "wait";
    } else if (slot.connected) {
      out->state = "ok";
    } else if (slot.circuit_breaker_tripped) {
      out->state = "fail";
    } else {
      out->state = "disc";
    }
  }
  out->publish_ok = slot.client ? slot.client->getPublishOk() : 0;
  out->publish_err = slot.client ? slot.client->getPublishErr() : 0;
  // Lets the portal show why a healthy slot is quiet.
  out->filter_mask = b->_obs ? b->_obs->mqtt_slot_packet_filter[slot_index]
                             : MQTTPacketFilter::kAllPacketTypes;
  return true;
}

int MQTTBridge::getMaxActiveSlots() {
  // Each WSS/TLS connection needs ~40KB for mbedTLS buffers. Without PSRAM even
  // 3 concurrent connections would exhaust internal heap, so cap at 2; with
  // PSRAM cap at 5 (6 configurable but 5 active max).
#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
  return psramFound() ? 5 : 2;
#else
  return 2;
#endif
}

uint8_t MQTTBridge::getLastWifiDisconnectReason() { return s_wifi_disconnect_reason; }
unsigned long MQTTBridge::getLastWifiDisconnectTime() { return s_wifi_disconnect_time; }

unsigned long MQTTBridge::getSlotCurrentOutageStartMs(int slot_index) const {
  if (slot_index < 0 || slot_index >= RUNTIME_MQTT_SLOTS) return 0;
  return _slots[slot_index].current_outage_started_ms;
}

bool MQTTBridge::isSlotEnabledAndAttempted(int slot_index) const {
  if (slot_index < 0 || slot_index >= RUNTIME_MQTT_SLOTS) return false;
  const MQTTSlot& s = _slots[slot_index];
  return s.enabled && s.initial_connect_done;
}

const char* MQTTBridge::getSlotPresetName(int slot_index) const {
  if (slot_index < 0 || slot_index >= RUNTIME_MQTT_SLOTS) return "?";
  const MQTTSlot& s = _slots[slot_index];
  if (s.preset && s.preset->name) return s.preset->name;
  if (!s.enabled) return MQTT_PRESET_NONE;
  return MQTT_PRESET_CUSTOM;
}

const char* MQTTBridge::wifiReasonStr(uint8_t reason) {
  switch (reason) {
    case 2:   return "auth expired";
    case 4:   return "assoc timeout";
    case 8:   return "AP disconnected";
    case 15:  return "4-way handshake timeout";
    case 18:  return "group cipher mismatch";
    case 40:  return "cipher suite rejected";
    case 49:  return "invalid PMKID";
    case 61:  return "AP BSS management";
    case 88:  return "AP BSS management";
    case 168: return "AP band-steering kick";
    case 34:  return "AP state mismatch (class 3 frame)";
    case 39:  return "SSID not found";
    case 63:  return "SA query timeout (PMF)";
    case 200: return "signal lost";
    case 201: return "security mismatch";
    case 202: return "auth mode rejected";
    case 204: return "handshake timeout";
    default:  return nullptr;
  }
}

const char* MQTTBridge::tlsErrorStr(int32_t err) {
  switch (err) {
    case 0x8001: return "DNS failed";
    case 0x8002: return "socket error";
    case 0x8004: return "connect refused";
    case 0x8006: return "TLS timeout";
    case 0x8008: return "connection timeout";
    case 0x800B: return "cert verify failed";
    case 0x8010: return "mbedTLS error";
    case 0x801A: return "TLS handshake failed";
    default:     return nullptr;
  }
}

void MQTTBridge::formatSlotDiagReply(char* buf, size_t bufsize, int slot_index) {
  if (!buf || bufsize == 0) return;
  if (!s_mqtt_bridge_instance || !s_mqtt_bridge_instance->_initialized) {
    snprintf(buf, bufsize, "> mqtt%d: bridge not running", slot_index + 1);
    return;
  }
  if (slot_index < 0 || slot_index >= RUNTIME_MQTT_SLOTS) {
    snprintf(buf, bufsize, "> invalid slot");
    return;
  }

  MQTTBridge* b = s_mqtt_bridge_instance;
  const MQTTSlot& slot = b->_slots[slot_index];

  // Determine state string
  const char* state;
  if (!slot.enabled && !slot.preset && slot.host[0] == '\0') {
    snprintf(buf, bufsize, "> mqtt%d: not configured", slot_index + 1);
    return;
  } else if (!slot.enabled) {
    state = "inactive";
  } else if (!b->isSlotReady(slot_index)) {
    // Same classification as `get mqtt.status` and getSlotStatusSnapshot(): the slot
    // is configured but missing a token/IATA/credential, so it was never set up and
    // has no client yet. Previously reported "disc", which read as a network fault.
    state = "wait";
  } else if (!slot.client) {
    // Ready to connect but the client object could not be allocated.
    state = "no client";
  } else if (slot.connected) {
    state = "ok";
  } else if (slot.circuit_breaker_tripped) {
    state = "fail";
  } else {
    state = "disc";
  }

  // replyAppendf clamps pos on every call, so the chained appends below can't
  // walk past the reply buffer even if the accumulated text exceeds it (A1).
  // A non-default filter is the one healthy-looking reason for a slot to stop
  // publishing, so it has to appear — but appended last. replyAppendf clamps at
  // the 160-byte reply, and an error tail (TLS + mbedTLS + errno + age) can
  // already reach ~117 chars, so putting the filter first would push the
  // operator's diagnostic detail off the end of a failing slot's line.
  const uint16_t filter_mask = b->_obs ? b->_obs->mqtt_slot_packet_filter[slot_index]
                                       : MQTTPacketFilter::kAllPacketTypes;
  char filter_text[MQTTPacketFilter::kFilterTextSize];
  const bool show_filter = filter_mask != MQTTPacketFilter::kAllPacketTypes &&
      MQTTPacketFilter::format(filter_mask, filter_text, sizeof(filter_text));

  int pos = 0;
  replyAppendf(buf, bufsize, &pos, "> mqtt%d: %s", slot_index + 1, state);
  if (slot.disconnect_count > 0) {
    replyAppendf(buf, bufsize, &pos, ", dc:%lu", (unsigned long)slot.disconnect_count);
    if (slot.first_disconnect_time > 0) {
      unsigned long first_disc_age_sec = (millis() - slot.first_disconnect_time) / 1000;
      replyAppendf(buf, bufsize, &pos, ", first_disc:%lus", first_disc_age_sec);
    }
  }

  // Connected with no errors: nothing more to say about the connection.
  if (slot.connected && slot.last_error_time == 0) {
    replyAppendf(buf, bufsize, &pos, ", no errors");
  } else if (slot.last_error_time > 0) {
    // TLS error with human-friendly description
    if (slot.last_tls_err != 0) {
      const char* desc = tlsErrorStr(slot.last_tls_err);
      if (desc) {
        replyAppendf(buf, bufsize, &pos, ", %s (0x%04X)", desc, (unsigned)slot.last_tls_err);
      } else {
        replyAppendf(buf, bufsize, &pos, ", tls:0x%04X", (unsigned)slot.last_tls_err);
      }
    }
    // mbedTLS stack error (shown as negative hex per convention)
    if (slot.last_tls_stack_err != 0) {
      replyAppendf(buf, bufsize, &pos, ", mbedtls:-0x%04X", (unsigned)(-slot.last_tls_stack_err));
    }
    // Socket errno
    if (slot.last_sock_errno != 0) {
      replyAppendf(buf, bufsize, &pos, ", sock:%d", slot.last_sock_errno);
    }
    // Time ago
    unsigned long ago_sec = (millis() - slot.last_error_time) / 1000;
    if (ago_sec < 60) {
      replyAppendf(buf, bufsize, &pos, ", %lus ago", ago_sec);
    } else if (ago_sec < 3600) {
      replyAppendf(buf, bufsize, &pos, ", %lum ago", ago_sec / 60);
    } else {
      replyAppendf(buf, bufsize, &pos, ", %luh ago", ago_sec / 3600);
    }
  } else if (!slot.connected) {
    replyAppendf(buf, bufsize, &pos, ", no error info");
  }

  // Appended last so it never displaces connection diagnostics. replyAppendf
  // clamps rather than overflows, but a clipped type list is worse than no
  // list: "…,13,14," parses as a real, different allowlist, and this is the
  // one line an operator reads to find out why a slot is quiet. So the exact
  // text is only emitted when it fits whole; otherwise fall back to a count,
  // which cannot be misread. `get mqttN.filter` always has the exact value.
  if (show_filter) {
    static const int kFilterLabelLen = (int)sizeof(", filter:") - 1;
    const int remaining = pos < (int)bufsize ? (int)bufsize - 1 - pos : 0;
    const uint8_t allowed = MQTTPacketFilter::countTypes(filter_mask);
    const int compact_len = kFilterLabelLen + (allowed >= 10 ? 5 : 4);  // "N/16"
    if (kFilterLabelLen + (int)strlen(filter_text) <= remaining) {
      replyAppendf(buf, bufsize, &pos, ", filter:%s", filter_text);
    } else if (compact_len <= remaining) {
      replyAppendf(buf, bufsize, &pos, ", filter:%u/16", (unsigned)allowed);
    }
    // Neither fits: the slot is reporting so much error detail that the filter
    // is the least useful field on the line. Omit it rather than mislead.
  }
}

// Bounded cooperative-stop timeout for end() (see MQTTLifecycle::Coordinator).
// Phase 0 hardware characterization (2026-07-19, Heltec V3 non-PSRAM + V4 PSRAM,
// see STABILITY_TESTABILITY_HANDOFF.md): a real mbedTLS/wss client teardown
// (disconnect + esp_mqtt_client_destroy) takes ~5-6 s per CONNECTED slot, applied
// SEQUENTIALLY in destroySlotClients(). So the safe timeout scales with the
// number of slots being torn down, not a single constant: a flat 8 s tripped the
// dirty/force-kill fallback on a healthy 2-slot non-PSRAM node (~11-12 s) and a
// normal 3-slot PSRAM node (~16 s), which withholds OTA on healthy devices.
//
// The budget below gives generous headroom (~8 s/slot vs the ~5-6 s measured)
// plus a fixed base for WiFi/queue/buffer teardown. Headroom is nearly free:
// end() returns as soon as the task acks (it checks _stop_acked before ticking
// the timeout), so a larger bound does NOT slow a healthy stop — it only length-
// ens the wait before force-killing a genuinely wedged task. The timeout is set
// per stop in end() via computeStopTimeoutMs() based on the enabled-slot count.
static const uint32_t MQTT_STOP_TIMEOUT_BASE_MS     = 5000;   // fixed teardown overhead
static const uint32_t MQTT_STOP_TIMEOUT_PER_SLOT_MS = 8000;   // ~5-6 s measured + headroom

// Slot-scaled cooperative-stop timeout. `slots` is the number of MQTT slots that
// will be torn down (enabled/connected); clamped to >=1 so a zero-slot bridge
// still budgets for the base teardown.
static inline uint32_t mqttStopTimeoutForSlots(int slots) {
  if (slots < 1) slots = 1;
  return MQTT_STOP_TIMEOUT_BASE_MS + MQTT_STOP_TIMEOUT_PER_SLOT_MS * (uint32_t)slots;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MQTTBridge::MQTTBridge(NodePrefs *prefs, MQTTPrefs *obs, mesh::PacketManager *mgr, mesh::RTCClock *rtc, mesh::LocalIdentity *identity)
    : BridgeBase(prefs, mgr, rtc),
      _obs(obs),
      _queue_count(0),
      _last_status_publish(0), _last_status_retry(0), _status_interval(300000),
      _ntp_client(_ntp_udp, effectiveNtpPrimary(obs), 0, 60000), _last_ntp_sync(0), _ntp_synced(false), _ntp_sync_pending(false), _slots_setup_done(false), _max_active_slots(RUNTIME_MQTT_SLOTS),
      _ntp_force_requested(false), _ntp_force_done(false), _ntp_force_result(false),
      _ntp_diag_requested(false), _ntp_diag_done(false), _ntp_diag_count(0),
      // Default to UTC; setRules() will be called from syncTimeWithNTP when a
      // non-UTC timezone string is configured. Timezone has no default ctor,
      // so we must pass rules here.
      _timezone_storage(TimeChangeRule{"UTC", Last, Sun, Mar, 0, 0}, TimeChangeRule{"UTC", Last, Sun, Mar, 0, 0}),
      _timezone(&_timezone_storage),
#if defined(BOARD_HAS_PSRAM)
      _last_raw_data(nullptr),
#endif
      _last_raw_len(0), _last_snr(0), _last_rssi(0), _last_raw_timestamp(0),
#if defined(BOARD_HAS_PSRAM)
      _json_scratch_buffer(nullptr),
#endif
      _identity(identity),
      _cached_has_connected_slots(false),
      _last_memory_check(0), _skipped_publishes(0),
      _last_no_broker_log(0), _queue_disconnected_since(0),
      _last_config_warning(0),
      _dispatcher(nullptr), _radio(nullptr), _board(nullptr), _ms(nullptr),
#ifdef WITH_SNMP
      _snmp_agent(nullptr),
#endif
      _last_wifi_check(0), _last_wifi_status(WL_DISCONNECTED), _wifi_status_initialized(false),
      _wifi_disconnected_time(0), _last_wifi_reconnect_attempt(0), _wifi_reconnect_backoff_attempt(0),
      _last_slot_reconnect_ms(0)
#ifdef ESP_PLATFORM
      , _packet_queue_handle(nullptr), _mqtt_task_handle(nullptr),
        _packet_queue_storage(nullptr)
#else
      , _queue_head(0), _queue_tail(0)
#endif
      // Cooperative lifecycle: _lifecycle_ops must be constructed before
      // _lifecycle (declaration order guarantees this) so the reference binds.
      // Seed with the worst-case (max runtime slots) budget; end() recomputes the
      // slot-scaled timeout before each stop via setStopTimeoutMs().
      , _lifecycle_ops(this), _lifecycle(_lifecycle_ops, mqttStopTimeoutForSlots(RUNTIME_MQTT_SLOTS))
{
  // Initialize default values
  strncpy(_origin, "MeshCore-Repeater", sizeof(_origin) - 1);
  strncpy(_iata, "XXX", sizeof(_iata) - 1);
  strncpy(_device_id, "DEVICE_ID_PLACEHOLDER", sizeof(_device_id) - 1);
  strncpy(_firmware_version, "unknown", sizeof(_firmware_version) - 1);
  strncpy(_board_model, "unknown", sizeof(_board_model) - 1);
  strncpy(_build_date, "unknown", sizeof(_build_date) - 1);
  _status_enabled = true;
  _packets_enabled = true;
  _raw_enabled = false;
  _rx_enabled = true;
  _tx_mode = 0;

  // Initialize all slots to empty/disabled state
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    memset(&_slots[i], 0, sizeof(MQTTSlot));
    _slots[i].enabled = false;
    _slots[i].client = nullptr;
    _slots[i].preset = nullptr;
    // auth_token == nullptr after memset above — allocated on first token creation
    _slots[i].connected = false;
    _slots[i].initial_connect_done = false;
    _slots[i].token_expires_at = 0;
    _slots[i].last_token_renewal = 0;
    _slots[i].reconnect_backoff = 0;
    _slots[i].max_backoff_failures = 0;
    _slots[i].circuit_breaker_tripped = false;
    _slots[i].last_reconnect_attempt = 0;
    _slots[i].last_log_time = 0;
    _slots[i].port = 1883;
    _slot_reconfigure_pending[i] = false;
    _status_publish_pending[i] = false;
  }

  // Reset CLI-requested forced NTP sync handshake (bridge object is reused across restarts)
  _ntp_force_requested = false;
  _ntp_force_done = false;
  _ntp_force_result = false;

  // Reset CLI-requested NTP diagnostic handshake
  _ntp_diag_requested = false;
  _ntp_diag_done = false;
  _ntp_diag_count = 0;

#if defined(WITH_MQTT_NEIGHBORS)
  // Neighbors publish handoff (buffer allocated in begin() after PSRAM probe).
  // std::atomic has no value-initializing default ctor pre-C++20, so set them here.
  _neighbors_json_buffer = nullptr;
  _neighbors_publish_len = 0;
  _neighbors_publish_pending.store(false, std::memory_order_relaxed);
  _neighbors_last_result.store(NBR_RESULT_NONE, std::memory_order_relaxed);
  _neighbors_phase.store(NBR_SCHEDULED, std::memory_order_relaxed);
  _neighbors_secs_until_next.store(0, std::memory_order_relaxed);
#endif

  // Initialize JWT username
  _jwt_username[0] = '\0';

  // Initialize packet queue (FreeRTOS queue will be created in begin())
  #ifdef ESP_PLATFORM
  // Queue and mutex will be created in begin()
  #else
  // Initialize circular buffer for non-ESP32 platforms
  memset(_packet_queue, 0, sizeof(_packet_queue));
#if defined(BOARD_HAS_PSRAM)
  for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
    _packet_queue[i].has_raw_data = false;
  }
#endif
  #endif

  // Non-PSRAM boards keep the raw cache inline for the bridge lifetime.
  // PSRAM boards allocate their runtime buffers in begin(), after PSRAM has
  // been probed/initialized, and release them in end().
  #if !defined(BOARD_HAS_PSRAM)
  memset(_last_raw_data, 0, sizeof(_last_raw_data));
  #endif
  // The shared JSON document needs no setup here: its pools are allocated lazily on
  // the first publish through _json_allocator and released by releaseRuntimeBuffers().
}

void MQTTBridge::allocateRuntimeBuffers() {
  #if defined(BOARD_HAS_PSRAM)
  // Keep each allocation independent. A nullptr is deliberately retained on
  // failure: status/packet publish paths already use stack fallbacks, and the
  // next begin() will retry only the missing buffer.
  _last_raw_data = static_cast<uint8_t*>(MQTTRuntimeBufferLifecycle::allocateIfMissing(
      _last_raw_data, LAST_RAW_DATA_SIZE, psram_malloc));
  _json_scratch_buffer = static_cast<char*>(MQTTRuntimeBufferLifecycle::allocateIfMissing(
      _json_scratch_buffer, PUBLISH_JSON_BUFFER_SIZE, psram_malloc));
  MQTT_DEBUG_PRINTLN("Runtime buffers: raw=%s json=%s",
      _last_raw_data ? "PSRAM" : "unavailable",
      _json_scratch_buffer ? "PSRAM" : "stack fallback");
  #endif

#if defined(WITH_MQTT_NEIGHBORS)
  // Persistent neighbors JSON buffer, heap-allocated on every board: too large to
  // keep inline in the bridge object the way the non-PSRAM status/packet buffers
  // are. psram_malloc() falls back to internal DRAM, so this works without PSRAM.
  // Unlike status/packet there is no stack fallback — a nullptr simply disables
  // publishing (requestPublishNeighbors/publishNeighbors both no-op on nullptr).
  _neighbors_json_buffer = static_cast<char*>(MQTTRuntimeBufferLifecycle::allocateIfMissing(
      _neighbors_json_buffer, NEIGHBORS_JSON_BUFFER_SIZE, psram_malloc));
  MQTT_DEBUG_PRINTLN("Neighbors buffer: %s",
      _neighbors_json_buffer ? "ready" : "unavailable");
#endif
}

void MQTTBridge::releaseRuntimeBuffers() {
  #if defined(BOARD_HAS_PSRAM)
  _last_raw_data = static_cast<uint8_t*>(MQTTRuntimeBufferLifecycle::release(
      _last_raw_data, psram_free));
  _json_scratch_buffer = static_cast<char*>(MQTTRuntimeBufferLifecycle::release(
      _json_scratch_buffer, psram_free));
  #endif

  // Drop the shared document's pools with the buffers. clear() destroys every pool
  // and resets the list to its inline array; the next publish reallocates. Holding
  // 4 KB of pool across a stopped bridge is pure overhead.
  _json_scratch_doc.clear();

#if defined(WITH_MQTT_NEIGHBORS)
  // Paired with the unconditional allocation in allocateRuntimeBuffers().
  _neighbors_json_buffer = static_cast<char*>(MQTTRuntimeBufferLifecycle::release(
      _neighbors_json_buffer, psram_free));
  _neighbors_publish_len = 0;
  _neighbors_publish_pending.store(false, std::memory_order_release);
#endif

  // Never pair a newly allocated raw buffer with metadata from a prior bridge
  // run. This also makes non-PSRAM restarts discard their stale raw cache.
  _last_raw_len = 0;
  _last_snr = 0;
  _last_rssi = 0;
  _last_raw_timestamp = 0;
}

// ---------------------------------------------------------------------------
// begin()
// ---------------------------------------------------------------------------
void MQTTBridge::begin() {
  MQTT_DEBUG_PRINTLN("Initializing MQTT Bridge...");

  // Idempotent start (Phase 5): a second begin() on an already-running bridge
  // would re-run allocation and re-create the task, leaking the previous
  // queue/task. Guard here instead of relying on caller discipline.
  if (_initialized) {
    MQTT_DEBUG_PRINTLN("MQTT Bridge already running - begin() ignored");
    return;
  }

  // PSRAM diagnostic - helps debug memory fragmentation on boards with external RAM
  #ifdef BOARD_HAS_PSRAM
  {
    bool psram_available = psramFound();
    size_t psram_size = 0;
    size_t psram_free = 0;
    if (psram_available) {
      psram_size = ESP.getPsramSize();
      psram_free = ESP.getFreePsram();
    }
    MQTT_DEBUG_PRINTLN("PSRAM: found=%s, size=%u, free=%u",
      psram_available ? "YES" : "NO", psram_size, psram_free);
    if (!psram_available) {
      MQTT_DEBUG_PRINTLN("PSRAM: board has PSRAM flag but psramFound()=false. "
        "Trying explicit psramInit()...");
      bool init_result = psramInit();
      MQTT_DEBUG_PRINTLN("PSRAM: psramInit() returned %s", init_result ? "true" : "false");
      if (init_result) {
        psram_size = ESP.getPsramSize();
        psram_free = ESP.getFreePsram();
        MQTT_DEBUG_PRINTLN("PSRAM: after init - size=%u, free=%u", psram_size, psram_free);
      }
    }
    // Log internal heap for comparison
    MQTT_DEBUG_PRINTLN("PSRAM: internal_free=%u, internal_max_alloc=%u",
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  }
  #else
  MQTT_DEBUG_PRINTLN("PSRAM: not configured for this board (no BOARD_HAS_PSRAM)");
  #endif

  // Limit active slots based on available memory (see getMaxActiveSlots()).
  _max_active_slots = getMaxActiveSlots();
  MQTT_DEBUG_PRINTLN("Max active slots: %d", _max_active_slots);

  // Check if WiFi credentials are configured first
  if (!isWiFiConfigValid(_obs)) {
    MQTT_DEBUG_PRINTLN("MQTT Bridge initialization skipped - WiFi credentials not configured");
    return;
  }

  // These are begin()/end()-scoped on PSRAM targets. Allocation happens after
  // the PSRAM probe above so a late psramInit() has taken effect.
  allocateRuntimeBuffers();

  refreshOriginFromPrefs();

  strncpy(_iata, _obs->mqtt_iata, sizeof(_iata) - 1);
  _iata[sizeof(_iata) - 1] = '\0';

  StrHelper::stripSurroundingQuotes(_iata, sizeof(_iata));

  // Convert IATA code to uppercase (IATA codes are conventionally uppercase)
  for (int i = 0; _iata[i]; i++) {
    _iata[i] = toupper(_iata[i]);
  }

  // Initial snapshot of the publish toggles. NOTE: the publish hot paths read
  // these live from _obs->mqtt_* (status/packets/raw/rx/tx) so a CLI/web `set`
  // takes effect without a bridge restart; these members are kept only for
  // startup logging/back-compat and are not the source of truth.
  _status_enabled = _obs->mqtt_status_enabled;
  _packets_enabled = _obs->mqtt_packets_enabled;
  _raw_enabled = _obs->mqtt_raw_enabled;
  _rx_enabled = _obs->mqtt_rx_enabled;
  _tx_mode = _obs->mqtt_tx_enabled;  // 0=off, 1=all, 2=advert
  // Set status interval to 5 minutes (300000 ms), or use preference if set and valid
  if (_obs->mqtt_status_interval >= 1000 && _obs->mqtt_status_interval <= 3600000) {
    _status_interval = _obs->mqtt_status_interval;
  } else {
    // Invalid or uninitialized value - fix it in preferences and use default
    _obs->mqtt_status_interval = 300000; // Fix the preference value
    _status_interval = 300000; // 5 minutes default
  }

  // Check for configuration mismatch: bridge.source=tx but mqtt.tx=off
  checkConfigurationMismatch();

  MQTT_DEBUG_PRINTLN("Config: Origin=%s, IATA=%s, Device=%s", _origin, _iata, _device_id);

  // Apply slot presets from preferences
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    const char* preset_name = _obs->mqtt_slot_preset[i];
    if (preset_name[0] != '\0' && strcmp(preset_name, MQTT_PRESET_NONE) != 0) {
      if (strcmp(preset_name, MQTT_PRESET_CUSTOM) == 0) {
        // Custom broker: copy host/port/username/password from prefs
        _slots[i].preset = nullptr;
        strncpy(_slots[i].host, _obs->mqtt_slot_host[i], sizeof(_slots[i].host) - 1);
        _slots[i].host[sizeof(_slots[i].host) - 1] = '\0';
        if (strlen(_slots[i].host) == 0) {
          MQTT_DEBUG_PRINTLN("MQTT%d: custom preset has no server configured, disabling", i + 1);
          _slots[i].enabled = false;
          continue;
        }
        _slots[i].enabled = true;
        _slots[i].port = _obs->mqtt_slot_port[i];
        strncpy(_slots[i].username, _obs->mqtt_slot_username[i], sizeof(_slots[i].username) - 1);
        _slots[i].username[sizeof(_slots[i].username) - 1] = '\0';
        strncpy(_slots[i].password, _obs->mqtt_slot_password[i], sizeof(_slots[i].password) - 1);
        _slots[i].password[sizeof(_slots[i].password) - 1] = '\0';
        strncpy(_slots[i].audience, _obs->mqtt_slot_audience[i], sizeof(_slots[i].audience) - 1);
        _slots[i].audience[sizeof(_slots[i].audience) - 1] = '\0';
      } else {
        const MQTTPresetDef* preset = findMQTTPreset(preset_name);
        if (preset) {
          _slots[i].enabled = true;
          _slots[i].preset = preset;
          if (mqttPresetNeedsSlotCredentials(preset)) {
            strncpy(_slots[i].username, _obs->mqtt_slot_username[i], sizeof(_slots[i].username) - 1);
            _slots[i].username[sizeof(_slots[i].username) - 1] = '\0';
            strncpy(_slots[i].password, _obs->mqtt_slot_password[i], sizeof(_slots[i].password) - 1);
            _slots[i].password[sizeof(_slots[i].password) - 1] = '\0';
          }
        } else {
          MQTT_DEBUG_PRINTLN("MQTT%d: unknown preset '%s', disabling", i + 1, preset_name);
          _slots[i].enabled = false;
        }
      }
    }
  }

  // Log slot configuration
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].enabled) {
      if (_slots[i].preset) {
        MQTT_DEBUG_PRINTLN("MQTT%d: preset=%s", i + 1, _slots[i].preset->name);
      } else {
        MQTT_DEBUG_PRINTLN("MQTT%d: custom=%s:%d", i + 1, _slots[i].host, _slots[i].port);
      }
    } else {
      MQTT_DEBUG_PRINTLN("MQTT%d: none", i + 1);
    }
  }

  #ifdef ESP_PLATFORM
  // Create FreeRTOS queue; use PSRAM storage when available
  #ifdef BOARD_HAS_PSRAM
  _packet_queue_storage = (uint8_t*)psram_malloc(MAX_QUEUE_SIZE * sizeof(QueuedPacket));
  if (_packet_queue_storage != nullptr) {
    _packet_queue_handle = xQueueCreateStatic(MAX_QUEUE_SIZE, sizeof(QueuedPacket), _packet_queue_storage, &_packet_queue_struct);
  } else {
    _packet_queue_handle = nullptr;
  }
  #else
  // Non-PSRAM: use inline class-member storage with static queue creation.
  // Eliminates a separate heap allocation, reducing startup fragmentation.
  _packet_queue_storage = _packet_queue_inline;
  _packet_queue_handle = xQueueCreateStatic(MAX_QUEUE_SIZE, sizeof(QueuedPacket),
                                             _packet_queue_storage, &_packet_queue_struct);
  #endif
  if (_packet_queue_handle == nullptr) {
    _packet_queue_handle = xQueueCreate(MAX_QUEUE_SIZE, sizeof(QueuedPacket));
  }
  if (_packet_queue_handle == nullptr) {
    MQTT_DEBUG_PRINTLN("Failed to create packet queue!");
    #if defined(BOARD_HAS_PSRAM)
    psram_free(_packet_queue_storage);
    #endif
    _packet_queue_storage = nullptr;
    releaseRuntimeBuffers();
    return;
  }

  // Create FreeRTOS task for MQTT/WiFi processing on Core 0
  #ifndef MQTT_TASK_CORE
  #define MQTT_TASK_CORE 0
  #endif
  #ifndef MQTT_TASK_STACK_SIZE
  #define MQTT_TASK_STACK_SIZE 8192
  #endif
  #ifndef MQTT_TASK_PRIORITY
  #define MQTT_TASK_PRIORITY 1
  #endif

  // Task stack: dynamic allocation (internal RAM). A PSRAM-backed stack was tried and
  // reverted — it resets some boards (e.g. Heltec V4) when the task runs from PSRAM.
  _mqtt_task_handle = nullptr;
  // Clear the cooperative-stop handshake before the new task starts reading it.
  // deliverStop() leaves _stop_requested latched true after a stop cycle, so a
  // restart must reset it or the fresh task would self-terminate immediately.
  _stop_requested = false;
  _stop_acked = false;
  BaseType_t create_result = xTaskCreatePinnedToCore(
    mqttTask,
    "MQTTBridge",
    MQTT_TASK_STACK_SIZE,
    this,
    MQTT_TASK_PRIORITY,
    &_mqtt_task_handle,
    MQTT_TASK_CORE
  );
  if (create_result != pdPASS) _mqtt_task_handle = nullptr;
  if (_mqtt_task_handle == nullptr) {
    MQTT_DEBUG_PRINTLN("Failed to create MQTT task!");
    vQueueDelete(_packet_queue_handle);
    _packet_queue_handle = nullptr;
    #if defined(BOARD_HAS_PSRAM)
    psram_free(_packet_queue_storage);
    #endif
    _packet_queue_storage = nullptr;
    releaseRuntimeBuffers();
    return;
  }

  MQTT_DEBUG_PRINTLN("MQTT task created on Core %d", MQTT_TASK_CORE);
  #else
  // Non-ESP32: Initialize WiFi directly (no task)
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setAutoConnect(true);
  WiFi.begin(_obs->wifi_ssid, _obs->wifi_password);

  // NOTE: Slot setup deferred until after NTP sync in loop()
  #endif

  // MQTT client objects are NOT allocated here. setupSlot() creates one on a slot's
  // first setup, so unconfigured and capped-off slots never cost their ~1.3 KB of
  // internal DRAM. Once created a client lives for the bridge's lifetime, so the
  // reconfigure/reconnect paths still reuse the same mbedTLS context instead of
  // churning ~40 KB of internal heap per cycle.

  // Sync the lifecycle Coordinator to Running now that all resources exist and
  // the task is created. Driven only on the success path: the failure rollbacks
  // above already free what they acquired and leave the bridge Stopped, so we
  // must not also fire the state machine's release effect there (double free).
  // A fresh start also clears any dirty-stop latch (re-enabling OTA flashing).
  _lifecycle.requestStart();    // Stopped -> Starting (startTask() is a no-op here)
  _lifecycle.onTaskStarted();   // Starting -> Running

  _initialized = true;
  s_mqtt_bridge_instance = this;
  MQTT_DEBUG_PRINTLN("MQTT Bridge initialized");
}

// ---------------------------------------------------------------------------
// end()
// ---------------------------------------------------------------------------
void MQTTBridge::end() {
  MQTT_DEBUG_PRINTLN("Stopping MQTT Bridge...");

  // Idempotent stop: nothing to tear down if we never started (or already stopped).
  if (!_initialized) {
    MQTT_DEBUG_PRINTLN("MQTT Bridge already stopped - end() ignored");
    return;
  }

  // Stop new diagnostic reads through the singleton before teardown begins.
  s_mqtt_bridge_instance = nullptr;

  // Size the stop timeout to the work about to happen: each enabled slot's
  // mbedTLS/wss client takes ~5-6 s to disconnect + destroy, sequentially (Phase
  // 0 hardware characterization). A flat bound force-killed healthy multi-slot
  // nodes and withheld OTA; the slot-scaled budget lets a normal teardown ack
  // cleanly. Count enabled slots (the ones that connect); a disabled slot's
  // client destroys quickly. Must run BEFORE requestStop() arms the window.
  int stop_slots = 0;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].enabled) stop_slots++;
  }
  _lifecycle.setStopTimeoutMs(mqttStopTimeoutForSlots(stop_slots));
  MQTT_DEBUG_PRINTLN("MQTT stop: %d enabled slot(s), timeout %lu ms",
                     stop_slots, (unsigned long)_lifecycle.stopTimeoutMs());

  // Cooperative shutdown (Phase 5). Request the stop, then let the lifecycle
  // Coordinator drive it. On ESP32 the MQTT task (Core 0) tears down its own
  // clients where the mbedTLS contexts live and acknowledges via _stop_acked;
  // the queue/buffer release happens inside LifecycleOps::releaseResources()
  // once the Coordinator reaches Stopped (clean ack OR the reviewed timeout
  // fallback). This replaces the former blind vTaskDelete that could kill the
  // task mid-mbedTLS and then free client buffers on a corrupted heap.
  _lifecycle.requestStop();  // Running -> StopRequested; deliverStop() sets _stop_requested

#ifdef ESP_PLATFORM
  // Wait (bounded) for the task to acknowledge. tick() synthesizes the timeout
  // fallback if the task never acks. Checking the ack first each iteration means
  // a stop that completes right as the timeout expires is still treated as clean.
  while (_lifecycle.isStopInProgress()) {
    if (_stop_acked) {
      _lifecycle.onTaskStopped();   // StopRequested -> Stopped (clean): releaseResources()
      break;
    }
    _lifecycle.tick();              // may fire StopTimedOut -> Stopped (dirty): releaseResources()
    if (!_lifecycle.isStopInProgress()) break;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
#else
  // Non-ESP32: the bridge runs cooperatively in loop(); there is no separate
  // task to signal. Drive straight to a clean Stopped and let releaseResources()
  // perform the (unchanged) synchronous teardown.
  _stop_acked = true;
  _lifecycle.onTaskStopped();
#endif

  // Timezone is inline class storage (_timezone_storage) — nothing to delete.
  // The shared JSON document's pools were freed by releaseRuntimeBuffers() above.
  _initialized = false;
  _slots_setup_done = false;  // Reset so deferred setup runs again on next begin()
  MQTT_DEBUG_PRINTLN("MQTT Bridge stopped (%s)",
                     _lifecycle.stopTimedOut() ? "forced/timeout - OTA blocked" : "clean");
}

// ---------------------------------------------------------------------------
// LifecycleOps — binds MQTTLifecycle::Ops (the pure, host-tested spec) to the
// FreeRTOS / PsychicMqttClient runtime. Every method runs on the loop task
// (Core 1): the Coordinator that calls them is driven only from begin()/end().
// ---------------------------------------------------------------------------
uint32_t MQTTBridge::LifecycleOps::nowMs() {
  return (uint32_t)millis();
}

void MQTTBridge::LifecycleOps::startTask() {
  // No-op: begin() owns task/queue/buffer creation and its rollback paths. The
  // Coordinator is synced to Running there via requestStart()/onTaskStarted().
}

void MQTTBridge::LifecycleOps::deliverStop() {
  // Clear any stale ack before raising the request (same ordering as the NTP
  // handshake: clear the done-flag, then set the request). The MQTT task polls
  // _stop_requested at the top of mqttTaskLoop().
  _b->_stop_acked = false;
  _b->_stop_requested = true;
}

void MQTTBridge::LifecycleOps::releaseResources() {
  MQTTBridge* b = _b;
#ifdef ESP_PLATFORM
  // stopTimedOut() is set before this effect fires (Coordinator::dispatch), so
  // it reliably distinguishes a clean ack from the timeout fallback.
  const bool dirty = b->_lifecycle.stopTimedOut();
  if (dirty && !b->_stop_acked) {
    // Reviewed fallback: the task never acknowledged (likely wedged in mbedTLS).
    // Force-kill it and tear down clients here on Core 1 — the pre-cooperative
    // behavior — accepting the heap risk. The dirty latch keeps OTA flashing
    // blocked (canFlashAfterStop() == false) so firmware is never written after
    // this path.
    if (b->_mqtt_task_handle != nullptr) {
      vTaskDelete(b->_mqtt_task_handle);
    }
    for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) b->teardownSlot(i);
    b->destroySlotClients();
  }
  // Clean path (or a task that acked right at the deadline): the MQTT task
  // already disconnected/deleted its clients on Core 0 and self-terminated, so
  // we must NOT touch slots here (that would be a cross-core double-delete).
  // Just drop our handle reference; FreeRTOS reclaims the self-deleted task's
  // dynamically-allocated stack/TCB in the idle task.
  b->_mqtt_task_handle = nullptr;

  // Drain and delete the FreeRTOS packet queue (value-copied packets, no
  // external pointers to clean up). Safe on Core 1: not a TLS resource.
  if (b->_packet_queue_handle != nullptr) {
    QueuedPacket queued;
    while (xQueueReceive(b->_packet_queue_handle, &queued, 0) == pdTRUE) {
      b->_queue_count--;
    }
    vQueueDelete(b->_packet_queue_handle);
    b->_packet_queue_handle = nullptr;
  }
  #if defined(BOARD_HAS_PSRAM)
  psram_free(b->_packet_queue_storage);
  #endif
  b->_packet_queue_storage = nullptr;
#else
  // Non-ESP32 circular buffer + synchronous client teardown (unchanged behavior).
  for (int i = 0; i < b->_queue_count; i++) {
    int index = (b->_queue_head + i) % MAX_QUEUE_SIZE;
    memset(&b->_packet_queue[index], 0, sizeof(QueuedPacket));
  }
  b->_queue_count = 0;
  b->_queue_head = 0;
  b->_queue_tail = 0;
  memset(b->_packet_queue, 0, sizeof(b->_packet_queue));
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) b->teardownSlot(i);
  b->destroySlotClients();
#endif

  b->releaseRuntimeBuffers();
}

void MQTTBridge::LifecycleOps::onStopComplete(bool clean) {
  MQTT_DEBUG_PRINTLN("MQTT stop %s", clean
                     ? "acknowledged (clean)"
                     : "TIMED OUT (dirty; OTA flashing withheld)");
}

// ---------------------------------------------------------------------------
// FreeRTOS task entry point
// ---------------------------------------------------------------------------
#ifdef ESP_PLATFORM
void MQTTBridge::mqttTask(void* parameter) {
  MQTTBridge* bridge = static_cast<MQTTBridge*>(parameter);
  if (bridge) {
    bridge->mqttTaskLoop();
  }
  // Task should never return, but if it does, delete itself
  vTaskDelete(nullptr);
}

void MQTTBridge::initializeWiFiInTask() {
  MQTT_DEBUG_PRINTLN("Initializing WiFi in MQTT task...");

  // Initialize WiFi
  WiFi.mode(WIFI_STA);

  // Enable automatic reconnection - ESP32 will handle reconnection automatically
  WiFi.setAutoReconnect(true);
  WiFi.setAutoConnect(true);

  // Set up WiFi event handlers for better diagnostics and immediate disconnection
  // detection. Register ONCE — the bridge is reused across restarts (e.g. stopped
  // for `ota check`/`ota update`, or `set mqtt…` reconfigure) and WiFi.onEvent()
  // never removes prior callbacks, so re-registering leaks handlers and duplicates
  // every log line.
  if (!_wifi_event_registered) {
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
      switch(event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
          MQTT_DEBUG_PRINTLN("WiFi connected: %s", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
          // Set flag to trigger NTP sync from loop() instead of doing it here
          if (!_ntp_synced && !_ntp_sync_pending) {
            _ntp_sync_pending = true;
          }
          break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
          s_wifi_disconnect_reason = info.wifi_sta_disconnected.reason;
          s_wifi_disconnect_time = millis();
          MQTT_DEBUG_PRINTLN("WiFi disconnected: reason %d", s_wifi_disconnect_reason);
          break;
        default:
          break;
      }
    });
    _wifi_event_registered = true;
  }

  // Only (re)start the WiFi association if it isn't already up. end() leaves the
  // STA link connected, so on a restart (e.g. after `ota check`) calling
  // WiFi.begin() again forces a needless disconnect/reconnect — which also races
  // the MQTT task's first DNS lookup (getaddrinfo fails until WiFi/DNS recovers).
  // When already connected, the deferred slot setup still fires in mqttTaskLoop()
  // because _ntp_synced persists across end() (only _slots_setup_done is reset).
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(_obs->wifi_ssid, _obs->wifi_password);
  } else if (!_ntp_synced && !_ntp_sync_pending) {
    _ntp_sync_pending = true;  // already connected but never synced — kick NTP now
  }

  // NOTE: Slot setup is deferred until after NTP sync in mqttTaskLoop().
  // JWT-auth slots need valid timestamps for token creation, and connecting
  // before NTP sync just wastes heap on TLS handshakes that will be rejected.

  MQTT_DEBUG_PRINTLN("WiFi initialization started in task");
}

// ---------------------------------------------------------------------------
// mqttTaskLoop() - main loop running on Core 0
// ---------------------------------------------------------------------------
void MQTTBridge::mqttTaskLoop() {
  // Initialize WiFi first
  initializeWiFiInTask();

  // Wait a bit for WiFi to start connecting
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Main task loop
  #ifdef MQTT_MEMORY_DEBUG
  static unsigned long last_agent_log = 0;
  #endif
  while (true) {
    // Cooperative stop (Phase 5). end() on the loop task (Core 1) set this flag.
    // Tear down our own clients HERE on Core 0 — where the mbedTLS/transport
    // state lives — instead of letting Core 1 free them after a blind
    // vTaskDelete. Acknowledge LAST so end() only frees the queue/buffers once
    // this teardown has completed, then self-terminate via the mqttTask()
    // trampoline (vTaskDelete(nullptr)).
    if (_stop_requested) {
      MQTT_DEBUG_PRINTLN("MQTT task: cooperative stop - tearing down clients on Core 0");
      for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
        teardownSlot(i);
      }
      destroySlotClients();
      _stop_acked = true;   // release semantics: set only after teardown is done
      return;
    }

    #ifdef MQTT_MEMORY_DEBUG
    // #region agent log
    unsigned long now_loop = millis();
    if (now_loop - last_agent_log >= 60000) {
      last_agent_log = now_loop;
      size_t free_h = ESP.getFreeHeap();
      size_t max_alloc = ESP.getMaxAllocHeap();
      unsigned long internal_f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      unsigned long spiram_f = 0;
      #ifdef BOARD_HAS_PSRAM
      spiram_f = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      #endif
      agentLogHeap("MQTTBridge.cpp:mqttTaskLoop", "mqtt_loop_60s", "H5", free_h, max_alloc, internal_f, spiram_f);
    }
    // #endregion
    #endif

    unsigned long now = millis();

    // Periodic heap + publish-health snapshot. Gated behind MQTT_MEMORY_DEBUG (a
    // dedicated diagnostics flag, NOT enabled on production or plain MQTT_DEBUG builds)
    // so it stays off by default — the same data is available on demand via the
    // `get mqtt.stats` CLI command (formatMqttStatsReply / logMemoryStatus()).
    #ifdef MQTT_MEMORY_DEBUG
    static unsigned long last_mem_log = 0;
    if (now - last_mem_log >= 30000) {
      last_mem_log = now;
      logMemoryStatus();
    }
    #endif

    bool wifi_just_connected = handleWiFiConnection(now);
    if (wifi_just_connected) {
      // WiFi recovered — reset last_reconnect_attempt for disconnected slots so they
      // retry immediately rather than waiting up to 5 min for backoff timers to expire.
      for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
        if (_slots[i].enabled && _slots[i].initial_connect_done && !_slots[i].connected) {
          _slots[i].last_reconnect_attempt = 0;
        }
      }
    }

    // Check for pending NTP sync (triggered from WiFi event handler)
    if (_ntp_sync_pending && WiFi.status() == WL_CONNECTED) {
      _ntp_sync_pending = false;
      syncTimeWithNTP();
    }

    // Retry NTP every 30s if initial sync failed (slots can't start without valid time)
    if (!_ntp_synced && WiFi.status() == WL_CONNECTED) {
      static unsigned long last_ntp_retry = 0;
      if (now - last_ntp_retry >= 30000) {
        last_ntp_retry = now;
        syncTimeWithNTP();
      }
    }

    // Process a CLI-requested forced NTP sync (queued from Core 1). Running it here
    // keeps all NTP I/O on Core 0; requestForcedNtpSync() blocks the CLI thread until
    // we publish the outcome below.
    if (_ntp_force_requested) {
      _ntp_force_requested = false;
      // primary_only: validate just the server that was set, so a typo fails fast.
      bool ok = syncTimeWithNTP(true, /*primary_only=*/true);
      _ntp_force_result = ok;
      _ntp_force_done = true;  // set last so the waiter sees a consistent result
    }

    // Process a CLI-requested NTP connectivity diagnostic (queued from Core 1).
    // Probe-only — never touches the system clock.
    if (_ntp_diag_requested) {
      _ntp_diag_requested = false;
      runNtpDiagProbe();
      _ntp_diag_done = true;  // set last so the waiter sees populated results
    }

    // Deferred slot setup: wait until NTP is synced so JWT tokens get valid timestamps.
    // This avoids wasted TLS handshakes that get rejected due to bad token times.
    if (_ntp_synced && !_slots_setup_done) {
      _slots_setup_done = true;

      // Redirect mbedTLS allocations to PSRAM to save ~40KB internal heap per TLS connection.
      // This is critical when running 3 concurrent WSS connections.
      #if defined(BOARD_HAS_PSRAM)
      mbedtls_platform_set_calloc_free(psram_calloc, psram_free);
      MQTT_DEBUG_PRINTLN("mbedTLS allocator redirected to PSRAM");
      #endif

      MQTT_DEBUG_PRINTLN("NTP synced, setting up MQTT slots (max %d active)...", _max_active_slots);
      for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
        if (_slots[i].enabled) {
          if (!canActivateSlot(i)) {
            MQTT_DEBUG_PRINTLN("MQTT%d skipped: max active slots (%d) reached", i + 1, _max_active_slots);
            _slots[i].enabled = false;  // Disable so other loops skip it
            continue;
          }
          char reason[80];
          if (!isSlotReady(i, reason, sizeof(reason))) {
            MQTT_DEBUG_PRINTLN("MQTT%d not ready - run '%s' to connect", i + 1, reason);
            continue;
          }
          // A slot that fails to activate consumes no position and stays enabled, so
          // maintainSlotConnections() retries it and a later healthy broker is not
          // starved by it on a capped board.
          if (!setupSlot(i)) continue;
          // Stagger connections: 5s between slots to avoid simultaneous TLS handshakes
          // which compete for ~40KB internal heap each
          if (i < RUNTIME_MQTT_SLOTS - 1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
          }
        }
      }
    }

    // Process pending slot reconfigures (queued from CLI on Core 1)
    for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
      if (_slot_reconfigure_pending[i]) {
        _slot_reconfigure_pending[i] = false;
        MQTT_DEBUG_PRINTLN("Applying deferred reconfigure for MQTT%d (preset: %s)", i + 1, _obs->mqtt_slot_preset[i]);
        applySlotPreset(i, _obs->mqtt_slot_preset[i]);
      }
    }

    // Publish on-connect status for slots whose onConnect callback fired since
    // the last loop. Raised on the esp-mqtt event task, consumed here on the
    // bridge task so the shared status doc/buffer/origin are only ever touched
    // from Core 0 (see the onConnect handler / A2). Clear before publishing so a
    // reconnect during the publish re-arms for the next loop rather than being
    // lost; publishStatusToSlot() re-checks slot.connected and no-ops if dropped.
    for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
      if (_status_publish_pending[i]) {
        _status_publish_pending[i] = false;
        publishStatusToSlot(i);
      }
    }

    // Maintain slot connections (token renewal, reconnect with backoff)
    maintainSlotConnections();

    // Process packet queue
    processPacketQueue();

#if defined(WITH_MQTT_NEIGHBORS)
    // Consume a pending neighbors snapshot handed over by the mesh (Core 1).
    // The pending flag stays raised across the whole publish so a second
    // request is rejected until this one completes (see requestPublishNeighbors).
    if (_neighbors_publish_pending.load(std::memory_order_acquire)) {
      bool ok = publishNeighbors();
      _neighbors_last_result.store(ok ? NBR_RESULT_OK : NBR_RESULT_FAIL,
                                   std::memory_order_relaxed);
      // MQTT_DEBUG_PRINTLN concatenates its format as a string literal, so the
      // argument must be a literal, not a ternary expression.
      if (ok) {
        MQTT_DEBUG_PRINTLN("Neighbors published");
      } else {
        MQTT_DEBUG_PRINTLN("Neighbors publish failed");
      }
      _neighbors_publish_pending.store(false, std::memory_order_release);
    }
#endif

#ifdef WITH_SNMP
    // SNMP agent loop — process incoming UDP requests
    if (_snmp_agent) {
      if (!_snmp_agent->isRunning() && WiFi.isConnected() && _obs->snmp_enabled) {
        _snmp_agent->begin(_obs->snmp_community);
        MQTT_DEBUG_PRINTLN("SNMP agent started on port 161 (community: %s)", _obs->snmp_community);
      }
      if (_snmp_agent->isRunning()) {
        // Update MQTT stats from this core
        int connected = 0;
        for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
          if (_slots[i].enabled && _slots[i].connected) connected++;
        }
        _snmp_agent->updateMQTTStats(connected, _queue_count, _skipped_publishes);
        _snmp_agent->loop();
      }
    }
#endif

    // Periodic configuration check (throttled to avoid spam)
    checkConfigurationMismatch();

    // Periodic NTP refresh (every hour) — lightweight, non-blocking.
    // Uses async SNTP instead of the heavy syncTimeWithNTP() which blocks Core 0
    // for up to 20+ seconds with DNS lookups, UDP sockets, and retry loops.
    if (WiFi.status() == WL_CONNECTED && now - _last_ntp_sync > 3600000) {
      refreshNTP();
    }

    // Publish status updates (handle millis() overflow correctly).
    // Read the toggle live from prefs (like mqtt.packets/rx/tx below) so a
    // CLI/web `set mqtt.status` change applies without a bridge restart.
    if (_obs->mqtt_status_enabled) {
      bool has_destinations = _cached_has_connected_slots;

      // Early exit if no destinations - skip all the expensive logic below
      if (!has_destinations) {
        if (_last_status_retry != 0) {
          _last_status_retry = 0;
        }
      } else {
        bool should_publish = false;

        // First, check if we need to respect retry interval (prevents spam when publish keeps failing)
        if (_last_status_retry != 0) {
          unsigned long retry_elapsed = (now >= _last_status_retry) ?
                                       (now - _last_status_retry) :
                                       (ULONG_MAX - _last_status_retry + now + 1);
          if (retry_elapsed < STATUS_RETRY_INTERVAL) {
            should_publish = false;
          } else {
            should_publish = true;
          }
        } else {
          if (_last_status_publish == 0) {
            should_publish = true;
          } else {
            unsigned long elapsed = (now >= _last_status_publish) ?
                                   (now - _last_status_publish) :
                                   (ULONG_MAX - _last_status_publish + now + 1);
            should_publish = (elapsed >= _status_interval);
          }
        }

        if (should_publish) {
          if (_last_status_publish != 0) {
            unsigned long elapsed = (now >= _last_status_publish) ?
                                   (now - _last_status_publish) :
                                   (ULONG_MAX - _last_status_publish + now + 1);
            MQTT_DEBUG_PRINTLN("Status publish timer expired (elapsed: %lu ms, interval: %lu ms)", elapsed, _status_interval);
          } else {
            MQTT_DEBUG_PRINTLN("Status publish attempt (first publish or retry)");
          }

          _last_status_retry = now;
          if (publishStatus()) {
            _last_status_publish = now;
            _last_status_retry = 0;
            MQTT_DEBUG_PRINTLN("Status published successfully, next publish in %lu ms", _status_interval);
          } else {
            MQTT_DEBUG_PRINTLN("Status publish failed, will retry in %lu ms", STATUS_RETRY_INTERVAL);
          }
        }
      }
    }

    // Update cached connection status periodically (every 5 seconds)
    // This ensures cache stays accurate even if callbacks miss updates
    static unsigned long last_slot_status_update = 0;
    if (now - last_slot_status_update > 5000) {
      updateCachedConnectionStatus();
      last_slot_status_update = now;
    }

    // Adaptive delay: 5 ms when packets are queued, 50 ms when idle.
    // The previous "status approaching" check (widening to 5 ms for 10 s before each status
    // publish) caused 2 000 unnecessary wakeups per interval; the 50 ms idle tick catches
    // the status deadline with at most 50 ms of extra latency, which is irrelevant at a
    // 5-minute interval.
    vTaskDelay(pdMS_TO_TICKS(_queue_count > 0 ? 5 : 50));
  }
}
#endif

// ---------------------------------------------------------------------------
// Slot management
// ---------------------------------------------------------------------------

// Allocate this slot's PsychicMqttClient and register its persistent callbacks.
// Called from setupSlot(), i.e. only for a slot that is enabled, within the active
// cap, and ready to connect — a client is ~1.3 KB of internal DRAM and does nothing
// at all until setupSlot() runs (the reconnect ladder is gated on
// initial_connect_done), so slots that are unconfigured or capped off never get one.
//
// Once created the object lives until destroySlotClients(): reconfiguring a slot
// (preset change, JWT renewal, reconnect) reuses it, so the mbedTLS context and its
// ~40 KB of internal-heap buffers are allocated once instead of every reconfigure.
// That context is created by connect(), not by this constructor, so deferring the
// allocation to first use costs nothing beyond the object itself.
bool MQTTBridge::ensureSlotClient(int index) {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return false;
  MQTTSlot& slot = _slots[index];
  if (slot.client != nullptr) return true;

  // nothrow: this framework builds with C++ exceptions enabled, so a plain new would
  // throw on exhaustion and panic the node. A slot that cannot get a client should
  // degrade to the "no client" diag state instead.
  slot.client = new (std::nothrow) PsychicMqttClient();
  if (slot.client == nullptr) {
    MQTT_DEBUG_PRINTLN("MQTT%d: out of memory allocating client", index + 1);
    return false;
  }
  slot.client->setAutoReconnect(false);  // we handle reconnect with our own backoff

  slot.client->onConnect([this, index](bool sessionPresent) {
    MQTT_DEBUG_PRINTLN("MQTT%d connected", index + 1);
    _slots[index].connected = true;
    // NOTE: reconnect_backoff / max_backoff_failures are NOT reset here.
    // A CONNACK alone doesn't prove the link is healthy — a broker that
    // accepts and then drops within seconds would reset the ladder every
    // cycle and retry at the 10 s rung forever, and each retry is a full
    // TLS session alloc/free (~40 KB of internal-heap churn, a known
    // fragmentation driver). The ladder is instead cleared by
    // maintainSlotConnection() once the connection has stayed up for
    // BACKOFF_STABLE_RESET_MS, so flapping endpoints keep their earned
    // backoff level. The breaker itself does clear now: while connected
    // the diag/status must not claim the slot gave up, and the next
    // disconnect should be governed by the (still-elevated) ladder.
    _slots[index].connected_at_ms = millis();
    _slots[index].circuit_breaker_tripped = false;
    _slots[index].last_tls_err = 0;
    _slots[index].last_tls_stack_err = 0;
    _slots[index].last_sock_errno = 0;
    _slots[index].last_error_time = 0;
    _slots[index].current_outage_started_ms = 0;  // clear current-outage timer for AlertReporter
    updateCachedConnectionStatus();  // bool store — safe from this (esp-mqtt) task
    // This callback runs on the client's esp-mqtt event task, not the bridge
    // task. Do NOT build/publish status here: publishStatusToSlot() writes the
    // shared _json_scratch_doc/_json_scratch_buffer/_origin that the periodic
    // publishStatus() uses on the bridge task, and two slots' callbacks could
    // race each other over them. Marshal the publish onto the bridge task via a
    // per-slot flag (see mqttTaskLoop consumer / A2).
    _status_publish_pending[index] = true;
  });
  slot.client->onDisconnect([this, index](bool sessionPresent) {
    MQTT_DEBUG_PRINTLN("MQTT%d disconnected", index + 1);
    _slots[index].disconnect_count++;
    if (_slots[index].first_disconnect_time == 0) {
      _slots[index].first_disconnect_time = millis();
    }
    if (_slots[index].current_outage_started_ms == 0) {
      _slots[index].current_outage_started_ms = millis();
    }
    _slots[index].connected = false;
    _slots[index].connected_at_ms = 0;  // stability clock only runs while connected
    updateCachedConnectionStatus();
  });
  slot.client->onError([this, index](esp_mqtt_error_codes error) {
    _slots[index].last_tls_err = error.esp_tls_last_esp_err;
    _slots[index].last_tls_stack_err = error.esp_tls_stack_err;
    _slots[index].last_sock_errno = error.esp_transport_sock_errno;
    _slots[index].last_error_time = millis();
    if (error.error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
      // Broker rejected the MQTT CONNECT itself — not a transport failure.
      // return code: 1=protocol, 2=client-id rejected, 3=server unavailable,
      // 4=bad username/password, 5=not authorized. Codes 3/4/5 point at a
      // server-side lockout or auth problem rather than the network.
      MQTT_DEBUG_PRINTLN("MQTT%d connection refused by broker (return code=%d)",
        index + 1, (int)error.connect_return_code);
    } else if (error.esp_tls_last_esp_err != 0 || error.esp_tls_stack_err != 0 || error.esp_transport_sock_errno != 0) {
      MQTT_DEBUG_PRINTLN("MQTT%d error: tls=%d, tls_stack=%d, sock=%d, type=%d",
        index + 1, error.esp_tls_last_esp_err, error.esp_tls_stack_err,
        error.esp_transport_sock_errno, error.error_type);
    } else {
      MQTT_DEBUG_PRINTLN("MQTT%d error: type=%d", index + 1, error.error_type);
    }
  });
  return true;
}

// Allocate this slot's JWT token buffer. Called only from createSlotAuthToken(), the
// sole writer, so a slot on a non-JWT preset (or no preset at all) never allocates.
//
// PSRAM where the board has it (psram_malloc falls back to internal DRAM otherwise),
// which is what moves the token off internal heap for slots that DO use JWT. Safe
// because the only readers are CPU copies on the bridge task: JWTHelper memcpy's the
// token in here, and esp-mqtt copies it out of _mqtt_cfg into its own internal-DRAM
// storage when connect() applies the config. No DMA, no ISR, and no cache-disabled
// window -- unlike the PSRAM task stack that reset Heltec V4 boards.
bool MQTTBridge::ensureSlotAuthToken(int index) {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return false;
  MQTTSlot& slot = _slots[index];
  const bool fresh = (slot.auth_token == nullptr);
  slot.auth_token = static_cast<char*>(MQTTRuntimeBufferLifecycle::allocateIfMissing(
      slot.auth_token, AUTH_TOKEN_SIZE, psram_malloc));
  if (slot.auth_token == nullptr) {
    MQTT_DEBUG_PRINTLN("MQTT%d: out of memory allocating auth token", index + 1);
    return false;
  }
  // Initialise only a newly allocated buffer. Clearing on every call would discard a
  // valid token at the start of each renewal, so a renewal that then failed inside
  // JWTHelper would leave the slot with an empty password where it previously kept
  // working credentials (JWTHelper writes the token only on success).
  if (fresh) slot.auth_token[0] = '\0';
  return true;
}

// Safe only once this slot's client is gone: setCredentials() gave the client this
// pointer, and esp-mqtt re-reads it from _mqtt_cfg on any later connect() that
// re-applies a dirtied config. See the MQTTSlot::auth_token comment.
void MQTTBridge::releaseSlotAuthToken(int index) {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return;
  MQTTSlot& slot = _slots[index];
  slot.auth_token = static_cast<char*>(
      MQTTRuntimeBufferLifecycle::release(slot.auth_token, psram_free));
  slot.token_expires_at = 0;
  slot.last_token_renewal = 0;
}

void MQTTBridge::destroySlotClients() {
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    MQTTSlot& slot = _slots[i];
    if (slot.client != nullptr) {
      if (slot.client->connected()) {
        slot.client->disconnect();
      }
      #ifdef ESP_PLATFORM
      vTaskDelay(pdMS_TO_TICKS(50));
      #else
      delay(50);
      #endif
      delete slot.client;
      slot.client = nullptr;
    }
    // Unconditional: only now is the token unreachable from the client's stored
    // config, and a token without a client would otherwise leak.
    releaseSlotAuthToken(i);
  }
}

int MQTTBridge::activatedSlotCount() const {
  int n = 0;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].enabled && _slots[i].initial_connect_done) n++;
  }
  return n;
}

bool MQTTBridge::canActivateSlot(int index) const {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return false;
  // Already holding a position (a reconfigure of a live slot) — no new position needed.
  if (_slots[index].enabled && _slots[index].initial_connect_done) return true;
  return activatedSlotCount() < _max_active_slots;
}

// Returns true only when the slot reached connect(). A false result leaves the slot
// enabled but not activated, so it holds no active-slot position and
// maintainSlotConnections() will retry it — the allocation failures below are transient
// memory conditions, not permanent misconfiguration.
bool MQTTBridge::setupSlot(int index) {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return false;
  MQTTSlot& slot = _slots[index];

  if (!slot.enabled) {
    teardownSlot(index);
    return false;
  }

  // Every failure below is a real attempt, so stamp it: the retry interval in
  // maintainSlotConnections() measures from last_reconnect_attempt, which starts at 0
  // and is re-zeroed by teardownSlot(). Left unstamped, the gate degenerates to
  // "uptime >= SLOT_SETUP_RETRY_INTERVAL" and a failure past that point is retried on
  // the very next maintenance pass — the same task iteration, for a live reconfigure.
  // The reconnect ladder never reads this field for an unactivated slot (it is gated
  // on initial_connect_done), so stamping here cannot perturb reconnect timing.

  // First setup for this slot allocates its persistent client; later ones reuse it.
  if (!ensureSlotClient(index)) {
    MQTT_DEBUG_PRINTLN("MQTT%d: client allocation failed - will retry", index + 1);
    slot.last_reconnect_attempt = millis();
    return false;
  }

  // Reconfigure path: if we're re-applying (e.g. after a preset change), stop
  // the existing connection cleanly first. The client object (and its mbedTLS
  // context) is reused; setCredentials / setServer below overwrite the config
  // fields in place before connect() restarts the ESP-IDF client.
  if (slot.initial_connect_done) {
    if (slot.client->connected()) {
      slot.client->disconnect();
    }
    // Clear TLS verification fields so a stale CA-bundle attach or cert
    // pointer from a prior preset doesn't override the new one.
    esp_mqtt_client_config_t* cfg = slot.client->getMqttConfig();
    #if ESP_IDF_VERSION_MAJOR == 5
    cfg->broker.verification.certificate = nullptr;
    cfg->broker.verification.certificate_len = 0;
    cfg->broker.verification.crt_bundle_attach = nullptr;
    cfg->credentials.username = nullptr;
    cfg->credentials.authentication.password = nullptr;
    #else
    cfg->cert_pem = nullptr;
    cfg->cert_len = 0;
    cfg->crt_bundle_attach = nullptr;
    cfg->username = nullptr;
    cfg->password = nullptr;
    #endif
    if (slot.auth_token) slot.auth_token[0] = '\0';
    slot.connected = false;
    slot.token_expires_at = 0;
    slot.last_token_renewal = 0;
    slot.reconnect_backoff = 0;
    slot.max_backoff_failures = 0;
    slot.circuit_breaker_tripped = false;
    slot.last_reconnect_attempt = 0;
  }

  bool uses_jwt = (slot.preset && slot.preset->auth_type == MQTT_AUTH_JWT) || slot.audience[0] != '\0';
  optimizeMqttClientConfig(slot.client, uses_jwt);  // sets keepalive (45s PSRAM, 75s non-PSRAM)
  #ifndef MQTT_FORCE_KEEPALIVE_45
  #if defined(BOARD_HAS_PSRAM)
  if (slot.preset && slot.preset->keepalive > 0) {
    slot.client->setKeepAlive(slot.preset->keepalive);  // preset overrides default
  }
  #else
  // Non-PSRAM: keep the longer 75s default to reduce TLS churn.
  // Preset keepalive (55s) is more aggressive than needed behind Cloudflare.
  #endif
  #endif

  if (slot.preset) {
    // Preset-based slot
    slot.client->setServer(slot.preset->server_url);
    if (slot.preset->ca_cert) {
      slot.client->setCACert(slot.preset->ca_cert);
    }

    // A JWT slot with no usable token would connect unauthenticated and be rejected.
    // Stay unactivated instead, so the retry path tries again — the failure is either
    // a transient token-buffer allocation or a JWTHelper error, not a config problem.
    if (slot.preset->auth_type == MQTT_AUTH_JWT) {
      if (!createSlotAuthToken(index) || !slot.auth_token || slot.auth_token[0] == '\0') {
        MQTT_DEBUG_PRINTLN("MQTT%d: no usable JWT token - will retry", index + 1);
        slot.last_reconnect_attempt = millis();
        return false;
      }
      slot.client->setCredentials(_jwt_username, slot.auth_token);
    } else if (slot.preset->auth_type == MQTT_AUTH_USERPASS) {
      const char* user = nullptr;
      const char* pass = slot.preset->userpass_password
                             ? slot.preset->userpass_password
                             : slot.password;
      if (mqttPresetUsesDevicePubkeyUsername(slot.preset)) {
        user = _device_id;  // never send "{pubkey}" literally
      } else if (slot.preset->userpass_username) {
        user = slot.preset->userpass_username;
      } else if (slot.username[0] != '\0') {
        user = slot.username;
      }
      if (user && user[0] != '\0' && pass && pass[0] != '\0') {
        slot.client->setCredentials(user, pass);
      }
    }
  } else {
    // Custom broker slot — build persistent URI
    // If host already has a scheme (mqtt://, mqtts://, ws://, wss://), preserve the full URI
    // (including optional path/query) and only inject :port when the authority has no explicit port.
    // Otherwise, infer protocol from port number.
    bool has_scheme = (strncmp(slot.host, "mqtt://", 7) == 0 ||
                       strncmp(slot.host, "mqtts://", 8) == 0 ||
                       strncmp(slot.host, "ws://", 5) == 0 ||
                       strncmp(slot.host, "wss://", 6) == 0);
    if (has_scheme) {
      const char* authority = strstr(slot.host, "://");
      authority = authority ? authority + 3 : slot.host;
      const char* path = strchr(authority, '/');
      const char* authority_end = path ? path : slot.host + strlen(slot.host);
      bool has_explicit_port = false;

      // Detect host:port in URI authority (IPv6 literals in [addr]:port are supported).
      if (authority < authority_end) {
        if (*authority == '[') {
          const char* close = (const char*)memchr(authority, ']', authority_end - authority);
          if (close && (close + 1) < authority_end && *(close + 1) == ':') {
            has_explicit_port = true;
          }
        } else {
          const char* colon = (const char*)memchr(authority, ':', authority_end - authority);
          if (colon != nullptr) {
            has_explicit_port = true;
          }
        }
      }

      if (has_explicit_port || slot.port == 0) {
        snprintf(slot.broker_uri, sizeof(slot.broker_uri), "%s", slot.host);
      } else {
        const size_t authority_len = (size_t)(authority_end - slot.host);
        snprintf(slot.broker_uri, sizeof(slot.broker_uri), "%.*s:%u%s",
                 (int)authority_len,
                 slot.host,
                 (unsigned)slot.port,
                 path ? path : "");
      }
    } else {
      const char* proto = "mqtt";
      if (slot.port == 8883) {
        proto = "mqtts";
      } else if (slot.port == 443) {
        proto = "wss";
      }
      snprintf(slot.broker_uri, sizeof(slot.broker_uri), "%s://%s:%d", proto, slot.host, slot.port);
    }
    slot.client->setServer(slot.broker_uri);
    MQTT_DEBUG_PRINTLN("MQTT%d custom broker URI: %s (host='%s', port=%u)",
      index + 1, slot.broker_uri, slot.host, (unsigned)slot.port);

    // Custom TLS/WSS slots need a CA bundle for server verification.
    // The bundle is loaded into the global s_crt_bundle exactly once to avoid
    // a use-after-free race: connect() launches an async FreeRTOS task, and
    // calling setCACertBundle() again from a later slot would free the global
    // crts array while a prior slot's TLS handshake may still be reading it.
    bool needs_tls = (strncmp(slot.broker_uri, "mqtts://", 8) == 0 ||
                      strncmp(slot.broker_uri, "wss://", 6) == 0);
    if (needs_tls) {
      if (!s_ca_bundle_loaded) {
        size_t bundle_len = 0;
        if (rootca_crt_bundle_start != nullptr &&
            rootca_crt_bundle_end != nullptr &&
            rootca_crt_bundle_end > rootca_crt_bundle_start) {
          bundle_len = static_cast<size_t>(rootca_crt_bundle_end - rootca_crt_bundle_start);
        }

        if (bundle_len > 0) {
          MQTT_DEBUG_PRINTLN("MQTT global CA bundle init: embedded bundle (%u bytes)",
            (unsigned)bundle_len);
          // Load the bundle into the global s_crt_bundle via the first client.
          // This is a one-time operation; subsequent clients reuse via attachArduinoCACertBundle.
          slot.client->setCACertBundle(rootca_crt_bundle_start, bundle_len);
          s_ca_bundle_loaded = true;
        } else {
          MQTT_DEBUG_PRINTLN("MQTT%d TLS: no embedded cert bundle available", index + 1);
        }
      } else {
        // Global bundle already loaded — just attach the callback for this client.
        slot.client->attachArduinoCACertBundle(true);
      }
      MQTT_DEBUG_PRINTLN("MQTT%d TLS verify: CA bundle %s", index + 1,
        s_ca_bundle_loaded ? "active" : "unavailable");
    } else {
      MQTT_DEBUG_PRINTLN("MQTT%d custom broker uses non-TLS transport", index + 1);
    }

    // Custom slot authentication: JWT if audience is set, else username/password
    if (slot.audience[0] != '\0') {
      // JWT auth for custom slot — same rule as the preset JWT path above.
      if (!createSlotAuthToken(index) || !slot.auth_token || slot.auth_token[0] == '\0') {
        MQTT_DEBUG_PRINTLN("MQTT%d: no usable JWT token - will retry", index + 1);
        slot.last_reconnect_attempt = millis();
        return false;
      }
      slot.client->setCredentials(_jwt_username, slot.auth_token);
      MQTT_DEBUG_PRINTLN("MQTT%d custom broker using JWT auth (audience: %s)", index + 1, slot.audience);
    } else if (strlen(slot.username) > 0) {
      slot.client->setCredentials(slot.username, slot.password);
    }
  }

  slot.client->connect();
  slot.initial_connect_done = true;
  return true;
}

// Disconnect the slot's MQTT client and clear per-connection state, but leave
// the client object alive so a subsequent setupSlot() can reuse its mbedTLS
// context. This is called both on reconfigure (preset change) and at shutdown;
// destruction of the underlying client happens once in destroySlotClients().
void MQTTBridge::teardownSlot(int index) {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return;
  MQTTSlot& slot = _slots[index];

  if (slot.client && slot.client->connected()) {
    slot.client->disconnect();
    #ifdef ESP_PLATFORM
    vTaskDelay(pdMS_TO_TICKS(50));
    #else
    delay(50);
    #endif
  }

  // Invalidate the token but keep the buffer: the client survives teardown and still
  // holds this pointer in its config (see MQTTSlot::auth_token).
  if (slot.auth_token) slot.auth_token[0] = '\0';
  slot.connected = false;
  slot.initial_connect_done = false;
  slot.broker_uri[0] = '\0';
  slot.token_expires_at = 0;
  slot.last_token_renewal = 0;
  slot.reconnect_backoff = 0;
  slot.max_backoff_failures = 0;
  slot.circuit_breaker_tripped = false;
  slot.last_reconnect_attempt = 0;
  slot.last_log_time = 0;
  slot.last_deferred_log_ms = 0;
}

void MQTTBridge::maintainSlotConnections() {
  if (!_identity) return;

  // Check WiFi status first
  if (WiFi.status() != WL_CONNECTED) return;

  unsigned long now_millis = millis();
  unsigned long current_time = time(nullptr);
  bool time_synced = (current_time >= 1000000000); // After year 2001

  // JWT tokens require valid timestamps
  unsigned long clock_sec = current_time;
  bool can_do_jwt = MQTTConnectionPolicy::jwtClockAvailable(
      _ntp_synced, static_cast<uint32_t>(clock_sec));

  // Count connected slots to inform reconnect decisions
  int connected_count = 0;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].enabled && _slots[i].connected) connected_count++;
  }

  // Only allow one reconnect attempt per maintenance cycle to avoid
  // multiple simultaneous TLS handshakes blocking the network stack.
  // Time-based guard: block reconnects if any slot reconnected within the last 15 s,
  // ensuring the previous TLS handshake (and its Core-0-expensive completion events)
  // finish before the next slot begins its own handshake.
  bool reconnect_attempted_this_cycle = MQTTConnectionPolicy::reconnectGuardActive(
      static_cast<uint32_t>(now_millis), static_cast<uint32_t>(_last_slot_reconnect_ms));
  // Only allow one full teardown+setup per cycle to limit heap fragmentation
  // when multiple slots fail simultaneously
  bool teardown_attempted_this_cycle = false;

  // At most one deferred setup retry per cycle: a successful one ends in connect(), so
  // this shares the "no simultaneous TLS handshakes" rule the reconnect guard enforces.
  bool setup_retry_this_cycle = false;

  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (!_slots[i].enabled) continue;

    // JWT slots need time sync before we can manage tokens
    bool slot_jwt = (_slots[i].preset && _slots[i].preset->auth_type == MQTT_AUTH_JWT) ||
                    (!_slots[i].preset && _slots[i].audience[0] != '\0');
    if (slot_jwt && !can_do_jwt) {
      continue;
    }

    // Enabled but never activated: setupSlot() failed on a client or token allocation,
    // or on token creation. The ladder below is gated on initial_connect_done and would
    // never revisit it, and maintenance used to skip clientless slots entirely, so
    // without this the slot stayed dead until a reconfigure or reboot. Only retried
    // after the initial pass has run, so the NTP-deferred setup order is preserved.
    if (!_slots[i].initial_connect_done) {
      if (_slots_setup_done && !setup_retry_this_cycle && !reconnect_attempted_this_cycle &&
          isSlotReady(i) && canActivateSlot(i) &&
          MQTTConnectionPolicy::elapsedMs(static_cast<uint32_t>(now_millis),
                                         static_cast<uint32_t>(_slots[i].last_reconnect_attempt))
              >= SLOT_SETUP_RETRY_INTERVAL) {
        _slots[i].last_reconnect_attempt = now_millis;
        setup_retry_this_cycle = true;
        MQTT_DEBUG_PRINTLN("MQTT%d retrying deferred setup (int_heap=%d)", i + 1,
                           (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        if (setupSlot(i)) {
          // A successful setup ends in connect(), so it spends this cycle's single
          // handshake allowance as well as arming the 15 s cross-slot guard. Without
          // the local flag, a disconnected slot later in this same pass would start a
          // second concurrent TLS handshake — the contention the guard exists to
          // prevent, and most damaging here because a failed allocation is why we are
          // retrying at all. A failed setup launches nothing and so spends only
          // setup_retry_this_cycle.
          _last_slot_reconnect_ms = now_millis;
          reconnect_attempted_this_cycle = true;
        }
      }
      continue;
    }
    if (!_slots[i].client) continue;

    maintainSlotConnection(i, now_millis, current_time, time_synced, reconnect_attempted_this_cycle, teardown_attempted_this_cycle);
  }
}

void MQTTBridge::maintainSlotConnection(int index, unsigned long now_millis, unsigned long current_time, bool time_synced, bool& reconnect_attempted, bool& teardown_attempted) {
  MQTTSlot& slot = _slots[index];

  // Forgive past failures only after the connection has proven stable.
  // 2 minutes covers at least one keepalive round-trip (keepalive is 75 s),
  // so a link that can't survive a single keepalive period never resets the
  // ladder. Flapping endpoints therefore stay at their earned backoff rung
  // (worst case the 300 s rung / 30-minute breaker probes) instead of
  // hammering full TLS handshakes at the 10 s rung — see the onConnect
  // handler in ensureSlotClient() for why this doesn't happen on CONNACK.
  if (slot.connected &&
      (slot.reconnect_backoff != 0 || slot.max_backoff_failures != 0) &&
      MQTTConnectionPolicy::stableConnection(static_cast<uint32_t>(now_millis),
                                             static_cast<uint32_t>(slot.connected_at_ms))) {
    MQTT_DEBUG_PRINTLN("MQTT%d stable for %lus - clearing reconnect backoff (was level %d)",
        index + 1, (now_millis - slot.connected_at_ms) / 1000UL, slot.reconnect_backoff);
    slot.reconnect_backoff = 0;
    slot.max_backoff_failures = 0;
  }

  // JWT token renewal (for preset JWT slots and custom slots with audience set)
  bool slot_uses_jwt = (slot.preset && slot.preset->auth_type == MQTT_AUTH_JWT) ||
                       (!slot.preset && slot.audience[0] != '\0');
  if (slot_uses_jwt) {
    // Renew (and below, reconnect) this many seconds before the token's exp
    // claim. Scaled to the slot's token lifetime — see renewalBufferSecs()
    // for why a flat 60 s lost the renewal race against brokers that enforce
    // exp on live sessions (waev's 55-minute tokens).
    const unsigned long renewal_buffer = MQTTConnectionPolicy::renewalBufferSecs(
        static_cast<uint32_t>(slotTokenLifetime(index)));
    bool token_needs_renewal = MQTTConnectionPolicy::tokenNeedsRenewal(
        time_synced, static_cast<uint32_t>(current_time),
        static_cast<uint32_t>(slot.token_expires_at),
        static_cast<uint32_t>(renewal_buffer));

    // Throttle renewal attempts to once per minute
    bool can_attempt_renewal = MQTTConnectionPolicy::renewalAttemptAllowed(
        static_cast<uint32_t>(now_millis), static_cast<uint32_t>(slot.last_token_renewal));

    if (token_needs_renewal && can_attempt_renewal) {
      slot.last_token_renewal = now_millis;

      unsigned long old_token_expires_at = slot.token_expires_at;

      if (createSlotAuthToken(index)) {
        MQTT_DEBUG_PRINTLN("MQTT%d token renewed", index + 1);

        // Bounce the connection while WE control the timing whenever the old
        // token is inside the renewal buffer — waiting for the broker to
        // enforce exp mid-session means a FIN plus a trip through the backoff
        // ladder instead of one clean reconnect. Same buffer as the renewal
        // trigger above, so a renewal implies a proactive reconnect.
        bool old_token_expired_or_imminent = !time_synced ||
                                            (old_token_expires_at == 0) ||
                                            (current_time >= old_token_expires_at) ||
                                            (time_synced && old_token_expires_at >= 1000000000 &&
                                             current_time >= (old_token_expires_at - renewal_buffer));

        if (old_token_expired_or_imminent || !slot.client->connected()) {
          // Disconnect + reconnect with fresh credentials, reusing existing client
          // to avoid internal heap leak/fragmentation from destroy/create cycles
          MQTT_DEBUG_PRINTLN("MQTT%d token renewal: reconnecting with fresh credentials", index + 1);
          if (slot.client->connected()) {
            slot.client->disconnect();  // stops the client internally
          }
          slot.client->setCredentials(_jwt_username, slot.auth_token);
          slot.client->connect();  // restart stopped client; reconnect() fails silently on a stopped client
          reconnect_attempted = true;
          _last_slot_reconnect_ms = now_millis;
          MQTT_DEBUG_PRINTLN("MQTT%d int_heap=%d at token renewal reconnect", index + 1,
              (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
          MQTT_DEBUG_PRINTLN("  radio_state=%d, last_rx=%lums ago",
              _radio ? _radio->getRadioState() : -1,
              (_radio && _radio->getLastRecvMillis() > 0) ? (_ms->getMillis() - _radio->getLastRecvMillis()) : 0);
        } else {
          // Token renewed but old one still valid — just update credentials for next reconnect
          slot.client->setCredentials(_jwt_username, slot.auth_token);
        }
      } else {
        MQTT_DEBUG_PRINTLN("MQTT%d token renewal failed", index + 1);
        slot.token_expires_at = 0;
      }
      return; // Token renewal handled connect; skip backoff logic below
    }
  }

  // Phase 4 (MQTT memory-defrag): the MIN_TLS_HEAP preflight was a workaround
  // for the fragmentation caused by per-reconnect mbedTLS allocations. With
  // persistent clients (Phase 1), the mbedTLS context is allocated once at
  // startup and the preflight is no longer necessary.

  // Periodic probe for circuit-breaker-tripped slots (recovery from transient outages)
  // Attempts a single reconnect every 30 minutes to see if the server has come back
  if (slot.circuit_breaker_tripped && !reconnect_attempted) {
    unsigned long probe_elapsed = MQTTConnectionPolicy::elapsedMs(
        static_cast<uint32_t>(now_millis), static_cast<uint32_t>(slot.last_reconnect_attempt));
    if (MQTTConnectionPolicy::circuitBreakerProbeDue(
            static_cast<uint32_t>(now_millis), static_cast<uint32_t>(slot.last_reconnect_attempt))) {
      slot.last_reconnect_attempt = now_millis;
      reconnect_attempted = true;
      _last_slot_reconnect_ms = now_millis;
      MQTT_DEBUG_PRINTLN("MQTT%d circuit breaker probe (attempting single reconnect after %lu ms, int_heap=%d)", index + 1, probe_elapsed,
          (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      MQTT_DEBUG_PRINTLN("  radio_state=%d, last_rx=%lums ago",
          _radio ? _radio->getRadioState() : -1,
          (_radio && _radio->getLastRecvMillis() > 0) ? (_ms->getMillis() - _radio->getLastRecvMillis()) : 0);
      if (slot_uses_jwt) {
        // Regenerate or refresh token, then reconnect the persistent client.
        // Reaching the ladder at all means setupSlot() ran, so the client object
        // and its mbedTLS context are live and no full setup is needed here.
        if (createSlotAuthToken(index)) {
          slot.client->setCredentials(_jwt_username, slot.auth_token);
          MQTT_DEBUG_PRINTLN("MQTT%d circuit breaker probe (fresh token)", index + 1);
        }
        slot.client->reconnect();
      } else {
        slot.client->reconnect();
      }
      // If the connect callback fires and sets slot.connected = true,
      // it will clear circuit_breaker_tripped via the onConnect handler
    }
  }

  // Reconnect with exponential backoff (for disconnected slots that already have valid config)
  // Only one reconnect per maintenance cycle to prevent TLS handshakes from blocking other slots
  if (!slot.connected && slot.initial_connect_done && !slot.circuit_breaker_tripped && !reconnect_attempted) {
    if (MQTTConnectionPolicy::reconnectDue(
            static_cast<uint32_t>(now_millis), static_cast<uint32_t>(slot.last_reconnect_attempt),
            slot.reconnect_backoff, static_cast<uint8_t>(index))) {
      slot.last_reconnect_attempt = now_millis;
      MQTTConnectionPolicy::BackoffAdvance advance = MQTTConnectionPolicy::advanceBackoff(
          slot.reconnect_backoff, slot.max_backoff_failures);
      slot.reconnect_backoff = advance.reconnect_backoff;
      slot.max_backoff_failures = advance.max_backoff_failures;
      slot.circuit_breaker_tripped = advance.circuit_breaker_tripped;
      if (!advance.should_reconnect) {
        MQTT_DEBUG_PRINTLN("MQTT%d circuit breaker tripped after %d failures at max backoff - stopping reconnect attempts. Reconfigure slot to retry.", index + 1, slot.max_backoff_failures);
        return;
      }
      MQTT_DEBUG_PRINTLN("MQTT%d reconnecting (backoff level %d, failures at max: %d, int_heap=%d)", index + 1, slot.reconnect_backoff, slot.max_backoff_failures,
          (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      MQTT_DEBUG_PRINTLN("  radio_state=%d, last_rx=%lums ago",
          _radio ? _radio->getRadioState() : -1,
          (_radio && _radio->getLastRecvMillis() > 0) ? (_ms->getMillis() - _radio->getLastRecvMillis()) : 0);
      reconnect_attempted = true;
      _last_slot_reconnect_ms = now_millis;
      if (slot_uses_jwt) {
        // Always lightweight reconnect on the persistent client. A stale/expired
        // token is handled by regenerating it in place and updating credentials
        // — no teardown is needed because the client and its mbedTLS context
        // persist for the bridge lifetime.
        if (createSlotAuthToken(index)) {
          slot.client->setCredentials(_jwt_username, slot.auth_token);
          MQTT_DEBUG_PRINTLN("MQTT%d reconnect (fresh token, backoff %d)", index + 1, slot.reconnect_backoff);
        } else {
          MQTT_DEBUG_PRINTLN("MQTT%d reconnect (token refresh failed, backoff %d)", index + 1, slot.reconnect_backoff);
        }
        slot.client->reconnect();
      } else {
        // Non-JWT slots — lightweight reconnect on existing client.
        MQTT_DEBUG_PRINTLN("MQTT%d reconnect (non-JWT, backoff %d)", index + 1, slot.reconnect_backoff);
        slot.client->reconnect();
      }
    }
  }
}

// Effective JWT lifetime for a slot: the preset's token_lifetime (or the 24 h
// default for custom/audience slots), minus the per-slot expiry stagger that
// keeps multiple JWT slots from renewing/reconnecting simultaneously. This is
// the exact value createSlotAuthToken() puts in the token's exp claim, so the
// renewal scheduling in maintainSlotConnection() can be derived from it.
unsigned long MQTTBridge::slotTokenLifetime(int index) const {
  const MQTTSlot& slot = _slots[index];
  unsigned long base_lifetime = MQTTConnectionPolicy::kDefaultJwtLifetimeSecs;
  if (slot.preset && slot.preset->auth_type == MQTT_AUTH_JWT && slot.preset->token_lifetime > 0) {
    base_lifetime = slot.preset->token_lifetime;
  }
  return MQTTConnectionPolicy::jwtLifetimeSecs(
      static_cast<uint32_t>(base_lifetime), static_cast<uint8_t>(index));
}

// How early (seconds before the token's exp claim) to renew the token AND
// proactively bounce the connection with fresh credentials. exp and the
// renewal schedule are locked together (both derive from slotTokenLifetime),
// so this buffer is the ONLY margin between "device re-authenticates" and
// "broker enforces exp and FIN-closes the session mid-stream" — shortening a
// preset's token_lifetime moves both times together and cannot widen it.
// The old flat 60 s lost that race whenever the device clock ran slow, or a
// single renewal attempt failed (the 60 s renewal throttle then ate the whole
// margin) — observed on the waev preset, whose 55-minute tokens are the only
// ones short enough for brokers to enforce exp against a live session.
// lifetime/10 with a 60 s floor and 300 s cap: 24 h tokens renew 5 min early
// (unchanged in practice), waev renews ~5 min early with ~5 throttled retry
// windows, and degenerate short lifetimes still renew inside their validity.
bool MQTTBridge::createSlotAuthToken(int index) {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return false;
  MQTTSlot& slot = _slots[index];
  if (!_identity) return false;

  // Determine JWT audience: preset takes priority, then custom slot audience field
  const char* audience = nullptr;
  if (slot.preset && slot.preset->auth_type == MQTT_AUTH_JWT) {
    audience = slot.preset->jwt_audience;
  } else if (slot.audience[0] != '\0') {
    audience = slot.audience;
  }
  if (!audience || audience[0] == '\0') return false;

  // This slot is confirmed JWT, so it needs the token buffer. Allocated on first use
  // and kept thereafter; every caller already treats false as "no usable token".
  if (!ensureSlotAuthToken(index)) return false;

  // Ensure JWT username is set
  if (_jwt_username[0] == '\0') {
    char public_key_hex[65];
    mesh::Utils::toHex(public_key_hex, _identity->pub_key, PUB_KEY_SIZE);
    snprintf(_jwt_username, sizeof(_jwt_username), "v1_%s", public_key_hex);
  }

  // Prepare owner key
  const char* owner_key = nullptr;
  char owner_key_uppercase[65];
  if (_obs->mqtt_owner_public_key[0] != '\0') {
    strncpy(owner_key_uppercase, _obs->mqtt_owner_public_key, sizeof(owner_key_uppercase) - 1);
    owner_key_uppercase[sizeof(owner_key_uppercase) - 1] = '\0';
    for (int i = 0; owner_key_uppercase[i]; i++) {
      owner_key_uppercase[i] = toupper(owner_key_uppercase[i]);
    }
    owner_key = owner_key_uppercase;
  }

  char client_version[64];
  getClientVersion(client_version, sizeof(client_version));
  const char* email = (_obs->mqtt_email[0] != '\0') ? _obs->mqtt_email : nullptr;

  unsigned long current_time = time(nullptr);
  unsigned long expires_in = slotTokenLifetime(index);  // preset/default lifetime minus per-slot stagger
  bool time_synced = (current_time >= 1000000000);

  if (JWTHelper::createAuthToken(
      *_identity, audience,
      0, expires_in, slot.auth_token, AUTH_TOKEN_SIZE,
      owner_key, client_version, email)) {
    slot.token_expires_at = time_synced ? (current_time + expires_in) : 0;
    return true;
  }

  slot.token_expires_at = 0;
  return false;
}

bool MQTTBridge::publishToSlot(int index, const char* topic, const char* payload, size_t payload_len, bool retained, uint8_t qos) {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return false;
  MQTTSlot& slot = _slots[index];
  if (!slot.client || !slot.connected) {
    unsigned long now = millis();
    if (now - slot.last_log_time > SLOT_LOG_INTERVAL) {
      slot.last_log_time = now;
      MQTT_DEBUG_PRINTLN("MQTT%d not connected - skipping publish", index + 1);
    }
    return false;
  }

  // Publish path by QoS:
  //  - QoS 0 (high-rate packets/raw): SYNCHRONOUS (async=false → esp_mqtt_client_publish),
  //    which writes straight to the socket. The async/outbox path drains only one queued
  //    item per esp-mqtt task loop (~1 msg/s/conn, gated by the 1s poll_read), so under
  //    even light packet load the outbox pins at its cap and drops ~20-30%. A synchronous
  //    write bypasses that drain ceiling entirely and does not store in the outbox. It can
  //    block the (Core-0, prio-1) MQTT task on a stalled socket, but only up to
  //    network_timeout_ms (lowered in optimizeMqttClientConfig); mesh RX (Core 1) and the
  //    WiFi/TCP stack (higher-prio system tasks) are unaffected, and a failed write flips
  //    the slot to disconnected so subsequent packets skip it.
  //  - QoS 1 (low-rate retained status): async, so it keeps the durable outbox + retransmit.
  //
  // Return convention: QoS 0 sync publish returns msg_id == 0 on success (no PUBACK
  // tracking). Negative values (-1 write/failure) are the only actual failures; the queue
  // retry/drop path below handles them.
  bool async = (qos > 0);
  int result = slot.client->publish(topic, qos, retained, payload, (int)payload_len, async);
  if (result < 0) {
    // QoS0 packet/raw publishes are best-effort and may be retried from the
    // bridge queue; avoid logging transient first-attempt failures here.
    if (qos > 0) {
      static unsigned long last_fail_log = 0;
      unsigned long now = millis();
      if (now - last_fail_log > 60000) {
        MQTT_DEBUG_PRINTLN("MQTT%d publish failed (result=%d qos=%u)", index + 1, result, (unsigned)qos);
        last_fail_log = now;
      }
    }
    return false;
  }
  return true;
}

bool MQTTBridge::publishToAllSlots(const char* topic, const char* payload, size_t payload_len, bool retained, uint8_t qos) {
  bool published = false;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].enabled && _slots[i].client && _slots[i].connected) {
      if (publishToSlot(i, topic, payload, payload_len, retained, qos)) {
        published = true;
      }
    }
  }
  return published;
}

// ---------------------------------------------------------------------------
// Topic building - resolves the correct topic for a given slot and message type.
// Presets use hardcoded topic logic; custom slots support user-defined templates.
// ---------------------------------------------------------------------------
bool MQTTBridge::substituteTopicTemplate(const char* tmpl, MQTTMessageType type, int slot_index, char* buf, size_t buf_size) {
  return mqttBuildPublicationTopic(MQTT_ROUTE_CUSTOM, (int)type, tmpl,
                                   _iata, _device_id, _obs->mqtt_slot_token[slot_index],
                                   buf, buf_size);
}

bool MQTTBridge::buildTopicForSlot(int index, MQTTMessageType type, char* topic_buf, size_t buf_size) {
  static_assert(
      static_cast<int>(MSG_STATUS) == MQTT_PUBLICATION_STATUS &&
      static_cast<int>(MSG_PACKETS) == MQTT_PUBLICATION_PACKETS &&
      static_cast<int>(MSG_RAW) == MQTT_PUBLICATION_RAW &&
      static_cast<int>(MSG_NEIGHBORS) == MQTT_PUBLICATION_NEIGHBORS,
      "topic router enum drift");

  if (!mqttTopicSlotIndexValid(index, RUNTIME_MQTT_SLOTS)) return false;
  const MQTTSlot& slot = _slots[index];

  // Preset slots: use hardcoded topic logic
  if (slot.preset) {
    MQTTTopicRouteStyle style = (slot.preset->topic_style == MQTT_TOPIC_MESHRANK)
        ? MQTT_ROUTE_MESHRANK : MQTT_ROUTE_MESHCORE;
    return mqttBuildPublicationTopic(style, (int)type, nullptr,
                                     _iata, _device_id, _obs->mqtt_slot_token[index],
                                     topic_buf, buf_size);
  }

  // Custom slots: use topic template if set, otherwise default meshcore format
  if (_obs->mqtt_slot_topic[index][0] != '\0') {
    return substituteTopicTemplate(_obs->mqtt_slot_topic[index], type, index, topic_buf, buf_size);
  }
  // Default: meshcore format
  return mqttBuildPublicationTopic(MQTT_ROUTE_MESHCORE, (int)type, nullptr,
                                   _iata, _device_id, _obs->mqtt_slot_token[index],
                                   topic_buf, buf_size);
}

void MQTTBridge::publishStatusToSlot(int index) {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return;
  MQTTSlot& slot = _slots[index];
  if (!slot.client || !slot.connected) return;

  refreshOriginFromPrefs();

  // Build per-slot topic (handles IATA check for meshcore, token check for meshrank)
  char status_topic[128];
  if (!buildTopicForSlot(index, MSG_STATUS, status_topic, sizeof(status_topic))) {
    return;  // Slot is missing required topic configuration
  }

  // Reuse pre-allocated buffer to avoid heap alloc/free churn under memory pressure.
  // _json_scratch_doc/_json_scratch_buffer/_origin are shared with publishStatus() and
  // with the packet/raw paths; every one of them runs only on the bridge task (this
  // function is reached solely via the _status_publish_pending consumer in
  // mqttTaskLoop, never from the onConnect callback thread — see A2), so the accesses
  // are serialized and need no mutex.
  #if defined(BOARD_HAS_PSRAM)
  char fallback_status_buffer[STATUS_JSON_BUFFER_SIZE];
  char* json_buffer = (_json_scratch_buffer != nullptr) ? _json_scratch_buffer : fallback_status_buffer;
  #else
  char* json_buffer = _json_scratch_buffer;
  #endif

  char origin_id[65];
  char timestamp[40];
  char radio_info[64];

  // Status timestamp: UTC with explicit +00:00 offset, same as packet/raw JSON
  // `timestamp` (system clock is UTC — SNTP offset 0; prefs Timezone is separate).
  struct timeval now_tv;
  gettimeofday(&now_tv, nullptr);
  MQTTMessageBuilder::formatIsoTimestampForMqtt(now_tv.tv_sec, now_tv.tv_usec, _timezone, timestamp, sizeof(timestamp));

  snprintf(radio_info, sizeof(radio_info), "%.6f,%.1f,%d,%d",
           _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);

  strncpy(origin_id, _device_id, sizeof(origin_id) - 1);
  origin_id[sizeof(origin_id) - 1] = '\0';

  char client_version[64];
  getClientVersion(client_version, sizeof(client_version));

  // Collect stats on-demand if sources are available
  int battery_mv = -1;
  int uptime_secs = -1;
  int errors = -1;
  int noise_floor = -999;
  int tx_air_secs = -1;
  int rx_air_secs = -1;
  int recv_errors = -1;
  int packets_sent = -1;
  int packets_received = -1;

  if (_board) battery_mv = _board->getBattMilliVolts();
  if (_ms) uptime_secs = _ms->getMillis() / 1000;
  if (_dispatcher) {
    errors = _dispatcher->getErrFlags();
    tx_air_secs = _dispatcher->getTotalAirTime() / 1000;
    rx_air_secs = _dispatcher->getReceiveAirTime() / 1000;
    packets_sent = (int)(_dispatcher->getNumSentFlood() + _dispatcher->getNumSentDirect());
    packets_received = (int)(_dispatcher->getNumRecvFlood() + _dispatcher->getNumRecvDirect());
  }
  if (_radio) {
    noise_floor = (int16_t)_radio->getNoiseFloor();
    recv_errors = (int)_radio->getPacketsRecvErrors();
  }

  // Internal heap free (for diagnosing repeater hangs from internal heap exhaustion)
  int internal_heap_free = (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

  int len = MQTTMessageBuilder::buildStatusMessage(
    _json_scratch_doc,
    _origin, origin_id, _board_model, _firmware_version, radio_info,
    client_version, "online", timestamp, json_buffer, STATUS_JSON_BUFFER_SIZE,
    battery_mv, uptime_secs, errors, _queue_count, noise_floor,
    tx_air_secs, rx_air_secs, recv_errors, internal_heap_free,
    packets_sent, packets_received,
    _prefs->disable_fwd ? "off" : "on"
  );

  if (len > 0) {
    // Honor the preset's retain policy, matching publishStatus() — brokers that
    // set allow_retain=false (e.g. waev) reject retained publishes, so this
    // on-connect status must not force retain=true. Custom slots default to
    // non-retained here too, keeping both status paths consistent.
    bool use_retain = slot.preset ? slot.preset->allow_retain : false;
    int result = slot.client->publish(status_topic, 1, use_retain, json_buffer, len);
    if (result <= 0) {
      MQTT_DEBUG_PRINTLN("MQTT%d status publish failed", index + 1);
    }
  }
}

void MQTTBridge::updateCachedConnectionStatus() {
  bool any_connected = false;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].enabled && _slots[i].connected) {
      any_connected = true;
      break;
    }
  }
  _cached_has_connected_slots = any_connected;
}

bool MQTTBridge::isAnySlotConnected() {
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].enabled && _slots[i].connected) {
      return true;
    }
  }
  return false;
}

void MQTTBridge::setSlotPreset(int slot_index, const char* preset_name) {
  if (slot_index < 0 || slot_index >= RUNTIME_MQTT_SLOTS) return;

  // On ESP32, teardown/setup involves TLS and must run on the MQTT task (Core 0).
  // Set a flag so the MQTT task picks it up on its next loop iteration.
  #ifdef ESP_PLATFORM
  if (_mqtt_task_handle != nullptr) {
    _slot_reconfigure_pending[slot_index] = true;
    MQTT_DEBUG_PRINTLN("MQTT%d reconfigure queued (preset: %s)", slot_index + 1, preset_name);
    return;
  }
  #endif

  // Non-ESP32 or bridge not yet started: apply directly
  applySlotPreset(slot_index, preset_name);
}

void MQTTBridge::applySlotPreset(int slot_index, const char* preset_name) {
  if (slot_index < 0 || slot_index >= RUNTIME_MQTT_SLOTS) return;
  MQTTSlot& slot = _slots[slot_index];

  teardownSlot(slot_index);

  if (strcmp(preset_name, MQTT_PRESET_NONE) == 0 || preset_name[0] == '\0') {
    slot.enabled = false;
    slot.preset = nullptr;
    return;
  }

  if (strcmp(preset_name, MQTT_PRESET_CUSTOM) == 0) {
    slot.preset = nullptr;
    // Re-sync every custom field from prefs (same copy begin() does at startup)
    // so a CLI/web edit to the host, port, credentials, or JWT audience is
    // actually picked up on reconfigure. Previously this branch reused the
    // stale slot fields, so e.g. changing mqttN.server or mqttN.username had no
    // effect on the live connection. Token and topic are read live from _obs in
    // setupSlot()/buildTopicForSlot(), so they don't need copying here.
    strncpy(slot.host, _obs->mqtt_slot_host[slot_index], sizeof(slot.host) - 1);
    slot.host[sizeof(slot.host) - 1] = '\0';
    slot.port = _obs->mqtt_slot_port[slot_index];
    strncpy(slot.username, _obs->mqtt_slot_username[slot_index], sizeof(slot.username) - 1);
    slot.username[sizeof(slot.username) - 1] = '\0';
    strncpy(slot.password, _obs->mqtt_slot_password[slot_index], sizeof(slot.password) - 1);
    slot.password[sizeof(slot.password) - 1] = '\0';
    strncpy(slot.audience, _obs->mqtt_slot_audience[slot_index], sizeof(slot.audience) - 1);
    slot.audience[sizeof(slot.audience) - 1] = '\0';
    slot.enabled = (slot.host[0] != '\0');
    if (_initialized && slot.enabled && customEndpointComplete(slot.host, slot.port)) {
      // Same cap startup applies. teardownSlot() above already released this slot's own
      // position, so reconfiguring a live slot still passes.
      if (!canActivateSlot(slot_index)) {
        MQTT_DEBUG_PRINTLN("MQTT%d skipped: max active slots (%d) reached", slot_index + 1, _max_active_slots);
        slot.enabled = false;
        return;
      }
      setupSlot(slot_index);
    }
    return;
  }

  const MQTTPresetDef* preset = findMQTTPreset(preset_name);
  if (preset) {
    slot.enabled = true;
    slot.preset = preset;
    if (mqttPresetNeedsSlotCredentials(preset)) {
      strncpy(slot.username, _obs->mqtt_slot_username[slot_index], sizeof(slot.username) - 1);
      slot.username[sizeof(slot.username) - 1] = '\0';
      strncpy(slot.password, _obs->mqtt_slot_password[slot_index], sizeof(slot.password) - 1);
      slot.password[sizeof(slot.password) - 1] = '\0';
    }
    if (_initialized) {
      char reason[80];
      if (!isSlotReady(slot_index, reason, sizeof(reason))) {
        MQTT_DEBUG_PRINTLN("MQTT%d (%s) not ready - run '%s' to connect", slot_index + 1, preset_name, reason);
        return;
      }
      // Same cap startup applies. Without this a live reconfigure could raise a
      // non-PSRAM board to three concurrent TLS sessions against a cap of two.
      if (!canActivateSlot(slot_index)) {
        MQTT_DEBUG_PRINTLN("MQTT%d skipped: max active slots (%d) reached", slot_index + 1, _max_active_slots);
        slot.enabled = false;
        return;
      }
      setupSlot(slot_index);
    }
  }
}

void MQTTBridge::setSlotCustomBroker(int slot_index, const char* host, uint16_t port,
                                      const char* username, const char* password) {
  if (slot_index < 0 || slot_index >= RUNTIME_MQTT_SLOTS) return;
  MQTTSlot& slot = _slots[slot_index];

  strncpy(slot.host, host ? host : "", sizeof(slot.host) - 1);
  slot.host[sizeof(slot.host) - 1] = '\0';
  slot.port = port;
  strncpy(slot.username, username ? username : "", sizeof(slot.username) - 1);
  slot.username[sizeof(slot.username) - 1] = '\0';
  strncpy(slot.password, password ? password : "", sizeof(slot.password) - 1);
  slot.password[sizeof(slot.password) - 1] = '\0';
}

// ---------------------------------------------------------------------------
// WiFi connection handling
// ---------------------------------------------------------------------------

void MQTTBridge::checkConfigurationMismatch() {
  // Warn if packets are enabled but both rx and tx are off — nothing will be published
  if (_obs->mqtt_packets_enabled && !_obs->mqtt_rx_enabled && _obs->mqtt_tx_enabled == 0) {
    unsigned long now = millis();
    if (_last_config_warning == 0 || (now - _last_config_warning > CONFIG_WARNING_INTERVAL)) {
      MQTT_DEBUG_PRINTLN("MQTT: Both mqtt.rx and mqtt.tx are off - no packets will be published. Run 'set mqtt.rx on' or 'set mqtt.tx on' to fix.");
      _last_config_warning = now;
    }
  } else {
    _last_config_warning = 0;
  }
}

bool MQTTBridge::handleWiFiConnection(unsigned long now) {
  wl_status_t current_wifi_status = WiFi.status();
  bool transitioned_to_connected = false;

  if (current_wifi_status == WL_CONNECTED && s_wifi_connected_at == 0) {
    s_wifi_connected_at = now;
  }
  if (!_wifi_status_initialized) {
    _last_wifi_status = current_wifi_status;
    _wifi_status_initialized = true;
    if (current_wifi_status != WL_CONNECTED) {
      _wifi_disconnected_time = now;
    }
  }
  if (now - _last_wifi_check <= 10000) {
    return false;
  }
  _last_wifi_check = now;

  if (current_wifi_status == WL_CONNECTED) {
    if (_last_wifi_status != WL_CONNECTED) {
      transitioned_to_connected = true;
      _wifi_disconnected_time = 0;
      s_wifi_connected_at = now;
      _wifi_reconnect_backoff_attempt = 0;
      #ifdef ESP_PLATFORM
      wifi_ps_type_t ps_mode;
      uint8_t ps_pref = _obs->wifi_power_save;
      if (ps_pref == 1) {
        ps_mode = WIFI_PS_NONE;
      } else if (ps_pref == 2) {
        ps_mode = WIFI_PS_MAX_MODEM;
      } else {
        ps_mode = WIFI_PS_NONE;  // default: no power save; eliminates DTIM wake latency on mains-powered bridges
      }
      esp_wifi_set_ps(ps_mode);
      #ifdef MQTT_WIFI_TX_POWER
      WiFi.setTxPower(MQTT_WIFI_TX_POWER);
      #else
      WiFi.setTxPower(WIFI_POWER_11dBm);
      #endif
      #endif
    }
    if (s_wifi_connected_at == 0) {
      s_wifi_connected_at = now;
    }
    _last_wifi_status = WL_CONNECTED;
  } else {
    if (_last_wifi_status == WL_CONNECTED) {
      _wifi_disconnected_time = now;
      s_wifi_connected_at = 0;
      // Disconnect all slot clients when WiFi drops
      for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
        if (_slots[i].client && _slots[i].connected) {
          _slots[i].client->disconnect();
        }
      }
    } else if (_wifi_disconnected_time > 0) {
      // Backoff ladder + wrap-safe timing live in MQTTConnectionPolicy (Phase 6),
      // exercised by host tests. Behavior is unchanged: both the link-down
      // duration and the since-last-attempt interval must clear the current rung
      // (elapsedMs is the wrap-safe form of the old ULONG_MAX branch).
      if (MQTTConnectionPolicy::wifiReconnectDue(
              (uint32_t)now, (uint32_t)_wifi_disconnected_time,
              (uint32_t)_last_wifi_reconnect_attempt,
              _wifi_reconnect_backoff_attempt)) {
        _last_wifi_reconnect_attempt = now;
        _wifi_reconnect_backoff_attempt =
            MQTTConnectionPolicy::nextWifiBackoffAttempt(_wifi_reconnect_backoff_attempt);
        WiFi.disconnect();
        WiFi.begin(_obs->wifi_ssid, _obs->wifi_password);
      }
    }
    _last_wifi_status = current_wifi_status;
  }
  return transitioned_to_connected;
}

bool MQTTBridge::isReady() const {
  return _initialized && isWiFiConfigValid(_obs);
}

bool MQTTBridge::isIATAValid() const {
  if (strlen(_iata) == 0 || strcmp(_iata, "XXX") == 0) {
    return false;
  }
  return true;
}

bool MQTTBridge::isSlotReady(int index, char* reason_buf, size_t reason_size) const {
  if (index < 0 || index >= RUNTIME_MQTT_SLOTS) return false;
  const MQTTSlot& slot = _slots[index];

  if (!slot.enabled) return true;  // disabled slots are "ready" (nothing to do)

  if (slot.preset) {
    if (slot.preset->topic_style == MQTT_TOPIC_MESHRANK) {
      if (_obs->mqtt_slot_token[index][0] == '\0') {
        if (reason_buf) snprintf(reason_buf, reason_size, "set mqtt%d.token <your_token>", index + 1);
        return false;
      }
    } else if (slot.preset->topic_style == MQTT_TOPIC_MESHCORE) {
      if (!isIATAValid()) {
        if (reason_buf) snprintf(reason_buf, reason_size, "set mqtt.iata <airport_code>");
        return false;
      }
    }
    if (mqttPresetNeedsSlotUsername(slot.preset) &&
        _obs->mqtt_slot_username[index][0] == '\0') {
      if (reason_buf) snprintf(reason_buf, reason_size, "set mqtt%d.username <user>", index + 1);
      return false;
    }
    if (mqttPresetNeedsSlotPassword(slot.preset) &&
        _obs->mqtt_slot_password[index][0] == '\0') {
      if (reason_buf) snprintf(reason_buf, reason_size, "set mqtt%d.password <pass>", index + 1);
      return false;
    }
  } else {
    // Custom slot without a topic template uses meshcore format, needs IATA
    if (_obs->mqtt_slot_topic[index][0] == '\0' && !isIATAValid()) {
      if (reason_buf) snprintf(reason_buf, reason_size, "set mqtt.iata <airport_code> or set mqtt%d.topic <template>", index + 1);
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// loop() - non-ESP32 main loop (ESP32 uses mqttTaskLoop via FreeRTOS task)
// ---------------------------------------------------------------------------
void MQTTBridge::loop() {
  if (!_initialized) return;

  #ifdef ESP_PLATFORM
  // On ESP32, loop() is a no-op - all processing happens in the FreeRTOS task
  return;
  #else
  unsigned long now = millis();
  if (handleWiFiConnection(now) && !_ntp_synced) {
    syncTimeWithNTP();
  }
  if (_ntp_sync_pending && WiFi.status() == WL_CONNECTED) {
    _ntp_sync_pending = false;
    syncTimeWithNTP();
  }

  // Deferred slot setup after NTP sync (non-ESP32 path)
  if (_ntp_synced && !_slots_setup_done) {
    _slots_setup_done = true;
    for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
      if (_slots[i].enabled) {
        if (!canActivateSlot(i)) {
          _slots[i].enabled = false;
          continue;
        }
        if (!isSlotReady(i)) {
          continue;
        }
        setupSlot(i);
      }
    }
  }

  // Process pending slot reconfigures
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slot_reconfigure_pending[i]) {
      _slot_reconfigure_pending[i] = false;
      applySlotPreset(i, _obs->mqtt_slot_preset[i]);
    }
  }

  // Maintain slot connections (token renewal, reconnect with backoff)
  maintainSlotConnections();

  // Process packet queue
  processPacketQueue();

  // Periodic configuration check (throttled to avoid spam)
  checkConfigurationMismatch();

  // Periodic NTP refresh (every hour) — lightweight, non-blocking.
  if (WiFi.status() == WL_CONNECTED && millis() - _last_ntp_sync > 3600000) {
    refreshNTP();
  }

  // Publish status updates (handle millis() overflow correctly).
  // Read the toggle live from prefs so a CLI/web `set mqtt.status` change
  // applies without a bridge restart.
  if (_obs->mqtt_status_enabled) {
    bool has_destinations = _cached_has_connected_slots;

    if (has_destinations) {
      unsigned long now = millis();
      bool should_publish = false;

      if (_last_status_retry != 0) {
        unsigned long retry_elapsed = (now >= _last_status_retry) ?
                                     (now - _last_status_retry) :
                                     (ULONG_MAX - _last_status_retry + now + 1);
        if (retry_elapsed >= STATUS_RETRY_INTERVAL) {
          should_publish = true;
        }
      } else {
        if (_last_status_publish == 0) {
          should_publish = true;
        } else {
          unsigned long elapsed = (now >= _last_status_publish) ?
                               (now - _last_status_publish) :
                               (ULONG_MAX - _last_status_publish + now + 1);
          should_publish = (elapsed >= _status_interval);
        }
      }

      if (should_publish) {
        if (_last_status_publish != 0) {
          unsigned long elapsed = (now >= _last_status_publish) ?
                                 (now - _last_status_publish) :
                                 (ULONG_MAX - _last_status_publish + now + 1);
          MQTT_DEBUG_PRINTLN("Status publish timer expired (elapsed: %lu ms, interval: %lu ms)", elapsed, _status_interval);
        } else {
          MQTT_DEBUG_PRINTLN("Status publish attempt (first publish or retry)");
        }

        _last_status_retry = now;
        if (publishStatus()) {
          _last_status_publish = now;
          _last_status_retry = 0;
          MQTT_DEBUG_PRINTLN("Status published successfully, next publish in %lu ms", _status_interval);
        } else {
          MQTT_DEBUG_PRINTLN("Status publish failed, will retry in %lu ms", STATUS_RETRY_INTERVAL);
        }
      }
    } else {
      if (_last_status_retry != 0) {
        _last_status_retry = 0;
      }
    }

    // Phase 4 (MQTT memory-defrag): the "recreate on prolonged status failure"
    // path and the periodic runCriticalMemoryCheckAndRecovery() call have been
    // removed. They were both symptoms of the heap churn introduced by
    // delete/new cycles of the MQTT client; with persistent clients the
    // allocator stays healthy and these recovery hooks aren't required.
  }
  #endif
}

// ---------------------------------------------------------------------------
// Packet handling
// ---------------------------------------------------------------------------

void MQTTBridge::onPacketReceived(mesh::Packet *packet) {
  if (!_initialized || !_obs->mqtt_packets_enabled || !_obs->mqtt_rx_enabled) return;

  // Drop before the queue copy when no configured slot allows this payload
  // type. A QueuedPacket carries the packet plus up to 256 bytes of raw radio
  // data and crosses to Core 0, so filtering here (not at publish time) is
  // what actually saves the queue slot and the memcpy. This also subsumes the
  // older "is any slot configured at all?" pre-check.
  bool filtered = false;
  if (!shouldQueuePacketType(packet->getPayloadType(), filtered)) {
    if (filtered) _filtered_packets++;
    return;
  }

  // Queue packet for transmission
  queuePacket(packet, false);
}

void MQTTBridge::sendPacket(mesh::Packet *packet) {
  uint8_t tx_mode = _obs->mqtt_tx_enabled;  // Read live from prefs (no restart needed)
  if (!_initialized || !_obs->mqtt_packets_enabled || tx_mode == 0) return;

  // Advert mode: only queue self-originated advert packets
  if (tx_mode == 2) {
    if (packet->getPayloadType() != PAYLOAD_TYPE_ADVERT) return;
    if (packet->payload_len < PUB_KEY_SIZE) return;
    // Advert payload starts with advertiser's 32-byte public key — compare to our identity
    if (!_identity || memcmp(_identity->pub_key, packet->payload, PUB_KEY_SIZE) != 0) return;
  }

  // Same pre-queue filter gate as the RX path.
  bool filtered = false;
  if (!shouldQueuePacketType(packet->getPayloadType(), filtered)) {
    if (filtered) _filtered_packets++;
    return;
  }

  // Queue packet for transmission
  queuePacket(packet, true);
}

void MQTTBridge::processPacketQueue() {
  #ifdef ESP_PLATFORM
  // Use FreeRTOS queue
  if (_packet_queue_handle == nullptr) {
    return;
  }

  // Update queue count from actual queue state
  _queue_count = uxQueueMessagesWaiting(_packet_queue_handle);

  if (_queue_count == 0) {
    _queue_disconnected_since = 0;
    return;
  }

  // Use cached connection status to avoid redundant checks
  bool has_connected_slots = _cached_has_connected_slots;

  if (!has_connected_slots) {
    if (_queue_count > 0) {
      unsigned long now = millis();
      if (now - _last_no_broker_log > NO_BROKER_LOG_INTERVAL) {
        MQTT_DEBUG_PRINTLN("Queue has %d packets but no slots connected", _queue_count);
        _last_no_broker_log = now;
      }
      // Flush stale packets after extended disconnect
      if (_queue_disconnected_since == 0) {
        _queue_disconnected_since = now;
      } else if (MQTTPacketQueuePolicy::shouldFlushDisconnected(
                     static_cast<uint32_t>(now),
                     static_cast<uint32_t>(_queue_disconnected_since))) {
        QueuedPacket discard;
        while (xQueueReceive(_packet_queue_handle, &discard, 0) == pdTRUE) {}
        _queue_count = 0;
        MQTT_DEBUG_PRINTLN("Flushed stale packet queue after %lu ms disconnected", now - _queue_disconnected_since);
        _queue_disconnected_since = now;
      }
    }
    return;
  }

  _queue_disconnected_since = 0;
  _last_no_broker_log = 0;

  // Adaptive drain: burst-process when queue has backlog, gentle otherwise
  int processed = 0;
  const MQTTPacketQueuePolicy::DrainBudget drain_budget =
      MQTTPacketQueuePolicy::drainBudget(static_cast<size_t>(_queue_count));
  unsigned long loop_start_time = millis();
  #ifdef MQTT_DIAG_VERBOSE
  static unsigned long last_retry_schedule_log = 0;
  #endif

  while (processed < drain_budget.max_packets) {
    if (!MQTTPacketQueuePolicy::drainTimeAvailable(
            static_cast<uint32_t>(millis()),
            static_cast<uint32_t>(loop_start_time),
            drain_budget.max_time_ms)) {
      break;
    }

    QueuedPacket queued;
    // Try to receive from queue (non-blocking)
    if (xQueueReceive(_packet_queue_handle, &queued, 0) != pdTRUE) {
      break;  // No more packets
    }

    unsigned long now_ms = millis();
    if (!MQTTPacketQueuePolicy::retryReady(
            static_cast<uint32_t>(now_ms),
            static_cast<uint32_t>(queued.next_retry_ms),
            queued.retry_attempts)) {
      // Not ready yet; put it back and stop draining this cycle.
      xQueueSend(_packet_queue_handle, &queued, 0);
      break;
    }

    // Update Core 0-owned last-raw-data for publishStatus() — no mutex needed since
    // _last_raw_data is now written only here (Core 0) and read only by publishStatus() (Core 0).
    if (!queued.is_tx && queued.has_raw_data && _last_raw_data) {
      memcpy(_last_raw_data, queued.raw_data, queued.raw_len);
      _last_raw_len       = queued.raw_len;
      _last_snr           = queued.snr;
      _last_rssi          = queued.rssi;
      _last_raw_timestamp = millis();
    }

    bool packet_eligible = false;
    bool packet_published = publishPacket(&queued.packet_copy, queued.is_tx,
                                          packet_eligible,
                                          queued.has_raw_data ? queued.raw_data : nullptr,
                                          queued.has_raw_data ? queued.raw_len  : 0,
                                          queued.snr, queued.rssi);
    taskYIELD();  // allow higher-priority tasks to run between packet publishes

    // Publish raw if enabled (live from prefs so `set mqtt.raw` applies without
    // a bridge restart)
    bool raw_eligible = false;
    bool raw_published = false;
    if (_obs->mqtt_raw_enabled) {
      raw_published = publishRaw(&queued.packet_copy, raw_eligible);
    }

    // Decide intentional completion once across the entire queue item. An
    // ineligible raw path (for example, MeshRank, which does not take raw)
    // must not hide a failed eligible structured publish.
    const bool queue_complete = MQTTPacketFilter::publishComplete(
        packet_eligible || raw_eligible,
        MQTTPacketQueuePolicy::queuedPacketPublished(packet_published, raw_published));
    const MQTTPacketQueuePolicy::RetryDecision retry =
        MQTTPacketQueuePolicy::retryDecision(
            queue_complete, queued.retry_attempts,
            static_cast<uint32_t>(now_ms));
    if (retry.action == MQTTPacketQueuePolicy::RetryAction::Schedule) {
      queued.retry_attempts = retry.retry_attempts;
      queued.next_retry_ms = retry.next_retry_ms;
      #ifdef MQTT_DIAG_VERBOSE
      if (now_ms - last_retry_schedule_log > 5000UL) {
        unsigned long age_ms = queued.timestamp > 0
            ? MQTTPacketQueuePolicy::elapsedMs(static_cast<uint32_t>(now_ms),
                                               static_cast<uint32_t>(queued.timestamp))
            : 0;
        MQTT_DEBUG_PRINTLN("Retry scheduled: attempt=%u/%u delay=%lu age=%lu q=%u pkt_type=%u packet_ok=%d raw_ok=%d",
          (unsigned)queued.retry_attempts, (unsigned)MQTTPacketQueuePolicy::kMaxQos0RetryAttempts,
          (unsigned long)retry.delay_ms, age_ms, (unsigned)uxQueueMessagesWaiting(_packet_queue_handle),
          (unsigned)queued.packet_copy.getPayloadType(), packet_published ? 1 : 0, raw_published ? 1 : 0);
        last_retry_schedule_log = now_ms;
      }
      #endif
      if (xQueueSend(_packet_queue_handle, &queued, 0) != pdTRUE) {
        MQTT_DEBUG_PRINTLN("Retry requeue failed, dropping packet (attempt=%u)", queued.retry_attempts);
      }
    } else if (retry.action == MQTTPacketQueuePolicy::RetryAction::Drop) {
      // Intentional: QoS0 best-effort packets are dropped silently in normal
      // builds; detailed exhaustion logs are only emitted in verbose mode.
      #ifdef MQTT_DIAG_VERBOSE
      static unsigned long last_retry_drop_log = 0;
      if (now_ms - last_retry_drop_log > 60000UL) {
        unsigned long age_ms = queued.timestamp > 0
            ? MQTTPacketQueuePolicy::elapsedMs(static_cast<uint32_t>(now_ms),
                                               static_cast<uint32_t>(queued.timestamp))
            : 0;
        MQTT_DEBUG_PRINTLN("Packet dropped after retry exhaustion (attempts=%u age=%lu pkt_type=%u packet_ok=%d raw_ok=%d)",
          queued.retry_attempts, age_ms, (unsigned)queued.packet_copy.getPayloadType(),
          packet_published ? 1 : 0, raw_published ? 1 : 0);
        last_retry_drop_log = now_ms;
      }
      #endif
    }

    _queue_count = uxQueueMessagesWaiting(_packet_queue_handle);
    processed++;
  }
  #else
  // Non-ESP32: Use circular buffer
  if (_queue_count == 0) {
    _queue_disconnected_since = 0;
    return;
  }

  bool has_connected_slots = _cached_has_connected_slots;

  if (!has_connected_slots) {
    if (_queue_count > 0) {
      unsigned long now = millis();
      if (now - _last_no_broker_log > NO_BROKER_LOG_INTERVAL) {
        MQTT_DEBUG_PRINTLN("Queue has %d packets but no slots connected", _queue_count);
        _last_no_broker_log = now;
      }
      if (_queue_disconnected_since == 0) {
        _queue_disconnected_since = now;
      } else if (MQTTPacketQueuePolicy::shouldFlushDisconnected(
                     static_cast<uint32_t>(now),
                     static_cast<uint32_t>(_queue_disconnected_since))) {
        while (_queue_count > 0) {
          dequeuePacket();
        }
        MQTT_DEBUG_PRINTLN("Flushed stale packet queue after %lu ms disconnected",
                           now - _queue_disconnected_since);
        _queue_disconnected_since = now;
      }
    }
    return;
  }

  _queue_disconnected_since = 0;
  _last_no_broker_log = 0;

  // Adaptive drain: burst-process when queue has backlog, gentle otherwise
  int processed = 0;
  const MQTTPacketQueuePolicy::DrainBudget drain_budget =
      MQTTPacketQueuePolicy::drainBudget(static_cast<size_t>(_queue_count));
  unsigned long loop_start_time = millis();
  #ifdef MQTT_DIAG_VERBOSE
  static unsigned long last_retry_schedule_log = 0;
  #endif

  while (_queue_count > 0 && processed < drain_budget.max_packets) {
    if (!MQTTPacketQueuePolicy::drainTimeAvailable(
            static_cast<uint32_t>(millis()),
            static_cast<uint32_t>(loop_start_time),
            drain_budget.max_time_ms)) {
      break;
    }

    QueuedPacket& queued = _packet_queue[_queue_head];
    unsigned long now_ms = millis();
    if (!MQTTPacketQueuePolicy::retryReady(
            static_cast<uint32_t>(now_ms),
            static_cast<uint32_t>(queued.next_retry_ms),
            queued.retry_attempts)) {
      break;
    }

    if (!queued.is_tx && queued.has_raw_data && _last_raw_data) {
      memcpy(_last_raw_data, queued.raw_data, queued.raw_len);
      _last_raw_len       = queued.raw_len;
      _last_snr           = queued.snr;
      _last_rssi          = queued.rssi;
      _last_raw_timestamp = millis();
    }

    bool packet_eligible = false;
    bool packet_published = publishPacket(&queued.packet_copy, queued.is_tx,
                                          packet_eligible,
                                          queued.has_raw_data ? queued.raw_data : nullptr,
                                          queued.has_raw_data ? queued.raw_len  : 0,
                                          queued.snr, queued.rssi);
    // No taskYIELD() on non-ESP32 platforms (non-FreeRTOS, cooperative scheduling not needed)

    // Live from prefs so `set mqtt.raw` applies without a bridge restart.
    bool raw_eligible = false;
    bool raw_published = false;
    if (_obs->mqtt_raw_enabled) {
      raw_published = publishRaw(&queued.packet_copy, raw_eligible);
    }

    // Decide intentional completion once across the entire queue item. An
    // ineligible raw path (for example, MeshRank, which does not take raw)
    // must not hide a failed eligible structured publish.
    const bool queue_complete = MQTTPacketFilter::publishComplete(
        packet_eligible || raw_eligible,
        MQTTPacketQueuePolicy::queuedPacketPublished(packet_published, raw_published));
    const MQTTPacketQueuePolicy::RetryDecision retry =
        MQTTPacketQueuePolicy::retryDecision(
            queue_complete, queued.retry_attempts,
            static_cast<uint32_t>(now_ms));
    if (retry.action == MQTTPacketQueuePolicy::RetryAction::Schedule) {
      queued.retry_attempts = retry.retry_attempts;
      queued.next_retry_ms = retry.next_retry_ms;
      #ifdef MQTT_DIAG_VERBOSE
      if (now_ms - last_retry_schedule_log > 5000UL) {
        unsigned long age_ms = queued.timestamp > 0
            ? MQTTPacketQueuePolicy::elapsedMs(static_cast<uint32_t>(now_ms),
                                               static_cast<uint32_t>(queued.timestamp))
            : 0;
        MQTT_DEBUG_PRINTLN("Retry scheduled: attempt=%u/%u delay=%lu age=%lu q=%d pkt_type=%u packet_ok=%d raw_ok=%d",
          (unsigned)queued.retry_attempts, (unsigned)MQTTPacketQueuePolicy::kMaxQos0RetryAttempts,
          (unsigned long)retry.delay_ms, age_ms, _queue_count,
          (unsigned)queued.packet_copy.getPayloadType(), packet_published ? 1 : 0, raw_published ? 1 : 0);
        last_retry_schedule_log = now_ms;
      }
      #endif
      break;  // keep packet at head for delayed retry
    } else if (retry.action == MQTTPacketQueuePolicy::RetryAction::Drop) {
      // Intentional: QoS0 best-effort packets are dropped silently in normal
      // builds; detailed exhaustion logs are only emitted in verbose mode.
      #ifdef MQTT_DIAG_VERBOSE
      static unsigned long last_retry_drop_log = 0;
      if (now_ms - last_retry_drop_log > 60000UL) {
        unsigned long age_ms = queued.timestamp > 0
            ? MQTTPacketQueuePolicy::elapsedMs(static_cast<uint32_t>(now_ms),
                                               static_cast<uint32_t>(queued.timestamp))
            : 0;
        MQTT_DEBUG_PRINTLN("Packet dropped after retry exhaustion (attempts=%u age=%lu pkt_type=%u packet_ok=%d raw_ok=%d)",
          queued.retry_attempts, age_ms, (unsigned)queued.packet_copy.getPayloadType(),
          packet_published ? 1 : 0, raw_published ? 1 : 0);
        last_retry_drop_log = now_ms;
      }
      #endif
    }

    dequeuePacket();
    processed++;
  }
  #endif
}

// ---------------------------------------------------------------------------
// Publishing
// ---------------------------------------------------------------------------

// Which slots will actually take this payload type, resolved in two stages so
// the cheap test runs first: the filter/enabled check costs a bit test, while
// the topic build costs an snprintf. Both are far cheaper than the ~2 KB JSON
// serialisation they gate, so this must complete before a caller builds a
// message — a slot that cannot form a topic (MeshRank raw, or a meshcore
// preset with no IATA set) would otherwise pay for a document nobody receives.
//
// A slot's connection state is deliberately not consulted: a configured but
// disconnected broker is still a target, so the packet stays on the queue for
// the existing bounded retry rather than being dropped as complete.
uint8_t MQTTBridge::eligiblePacketSlots(uint8_t packet_type, MQTTMessageType type) {
  static_assert(RUNTIME_MQTT_SLOTS <= 8, "eligible slot mask must fit in uint8_t");
  // Per-slot filters cover packet traffic only. Status and neighbors are
  // documented as never filtered, so refuse those publication types here
  // rather than silently applying a packet mask if a caller is added later.
  if (!_obs || (type != MSG_PACKETS && type != MSG_RAW)) return 0;

  uint8_t eligible_slots = 0;
  char topic[128];
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; ++i) {
    // Configuration is the gate, not client allocation: a slot whose client has not
    // been created yet (setupSlot() runs only after NTP sync) is still a target, so
    // the packet stays queued for the bounded retry per the note above.
    const bool slot_enabled = _slots[i].enabled;
    // Load once so a live CLI/WebConfig update cannot split this packet's
    // decision across two different masks.
    const uint16_t filter_mask = _obs->mqtt_slot_packet_filter[i];
    if (!MQTTPacketFilter::slotCandidate(slot_enabled, filter_mask, packet_type)) continue;

    const bool topic_supported = buildTopicForSlot(i, type, topic, sizeof(topic));
    if (MQTTPacketFilter::slotEligible(slot_enabled, topic_supported,
                                       filter_mask, packet_type)) {
      eligible_slots |= static_cast<uint8_t>(1u << i);
    }
  }
  return eligible_slots;
}

// Conservative pre-queue gate: the OR of every configured slot's mask. Runs on
// Core 1 in the radio callback, so it must stay cheap — a packet no broker
// wants is rejected before it is copied into the queue at all. `filtered`
// separates "a broker is configured but none wants this type" (worth counting)
// from "no broker configured at all" (the pre-existing silent drop).
bool MQTTBridge::shouldQueuePacketType(uint8_t packet_type, bool& filtered) {
  filtered = false;
  if (!_obs) return false;

  uint16_t masks[RUNTIME_MQTT_SLOTS];
  bool enabled[RUNTIME_MQTT_SLOTS];
  bool any_enabled = false;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; ++i) {
    masks[i] = _obs->mqtt_slot_packet_filter[i];
    // Configured, not allocated — see eligiblePacketSlots(). Gating on the client
    // here would silently drop every packet received before the post-NTP-sync slot
    // setup, which is exactly the window the queue exists to cover.
    enabled[i] = _slots[i].enabled;
    any_enabled = any_enabled || enabled[i];
  }
  if (!any_enabled) return false;

  if (MQTTPacketFilter::allows(
          MQTTPacketFilter::enabledUnion(masks, enabled, RUNTIME_MQTT_SLOTS),
          packet_type)) {
    return true;
  }
  filtered = true;
  return false;
}

bool MQTTBridge::publishStatus() {
  if (!_cached_has_connected_slots) {
    return false;
  }

  refreshOriginFromPrefs();

  // Reuse pre-allocated buffer to avoid heap alloc/free churn under memory pressure.
  // _json_scratch_buffer and _last_raw_data are both Core 0-owned; no mutex needed.
  #if defined(BOARD_HAS_PSRAM)
  char fallback_status_buffer[STATUS_JSON_BUFFER_SIZE];
  char* json_buffer = (_json_scratch_buffer != nullptr) ? _json_scratch_buffer : fallback_status_buffer;
  #else
  char* json_buffer = _json_scratch_buffer;
  #endif
  char origin_id[65];
  char timestamp[40];
  char radio_info[64];

  // Status timestamp: UTC with explicit +00:00 offset, same as packet/raw JSON
  // `timestamp` (system clock is UTC — SNTP offset 0; prefs Timezone is separate).
  struct timeval now_tv;
  gettimeofday(&now_tv, nullptr);
  MQTTMessageBuilder::formatIsoTimestampForMqtt(now_tv.tv_sec, now_tv.tv_usec, _timezone, timestamp, sizeof(timestamp));

  snprintf(radio_info, sizeof(radio_info), "%.6f,%.1f,%d,%d",
           _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);

  strncpy(origin_id, _device_id, sizeof(origin_id) - 1);
  origin_id[sizeof(origin_id) - 1] = '\0';

  char client_version[64];
  getClientVersion(client_version, sizeof(client_version));

  // Collect stats on-demand if sources are available
  int battery_mv = -1;
  int uptime_secs = -1;
  int errors = -1;
  int noise_floor = -999;
  int tx_air_secs = -1;
  int rx_air_secs = -1;
  int recv_errors = -1;
  int packets_sent = -1;
  int packets_received = -1;

  if (_board) battery_mv = _board->getBattMilliVolts();
  if (_ms) uptime_secs = _ms->getMillis() / 1000;
  if (_dispatcher) {
    errors = _dispatcher->getErrFlags();
    tx_air_secs = _dispatcher->getTotalAirTime() / 1000;
    rx_air_secs = _dispatcher->getReceiveAirTime() / 1000;
    packets_sent = (int)(_dispatcher->getNumSentFlood() + _dispatcher->getNumSentDirect());
    packets_received = (int)(_dispatcher->getNumRecvFlood() + _dispatcher->getNumRecvDirect());
  }
  if (_radio) {
    noise_floor = (int16_t)_radio->getNoiseFloor();
    recv_errors = (int)_radio->getPacketsRecvErrors();
  }

  // Internal heap free (for diagnosing repeater hangs from internal heap exhaustion)
  int internal_heap_free = (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

  int len = MQTTMessageBuilder::buildStatusMessage(
    _json_scratch_doc,
    _origin, origin_id, _board_model, _firmware_version, radio_info,
    client_version, "online", timestamp, json_buffer, STATUS_JSON_BUFFER_SIZE,
    battery_mv, uptime_secs, errors, _queue_count, noise_floor,
    tx_air_secs, rx_air_secs, recv_errors, internal_heap_free,
    packets_sent, packets_received,
    _prefs->disable_fwd ? "off" : "on"
  );

  if (len > 0) {
    bool published = false;
    bool any_slot_wants_status = false;
    char topic[128];
    for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
      if (_slots[i].enabled && _slots[i].client && _slots[i].connected) {
        if (buildTopicForSlot(i, MSG_STATUS, topic, sizeof(topic))) {
          any_slot_wants_status = true;
          bool use_retain = _slots[i].preset ? _slots[i].preset->allow_retain : false;
          if (publishToSlot(i, topic, json_buffer, (size_t)len, use_retain, 1)) {
            published = true;
          }
        }
      }
    }
    // If no connected slot accepts status topics, treat as success to avoid
    // infinite retry loops
    if (published || !any_slot_wants_status) {
      if (published) MQTT_DEBUG_PRINTLN("Status published");
      return true;
    }
  }

  return false;
}

bool MQTTBridge::publishPacket(mesh::Packet* packet, bool is_tx,
                                bool& has_eligible_target,
                                const uint8_t* raw_data, int raw_len,
                                float snr, float rssi) {
  has_eligible_target = false;
  if (!packet) return false;

  const uint8_t packet_type = packet->getPayloadType();
  const uint8_t eligible_slots = eligiblePacketSlots(packet_type, MSG_PACKETS);
  has_eligible_target = eligible_slots != 0;
  // Filtered out everywhere, or no slot can form a packets topic: intentionally
  // complete, and nothing below (JSON build included) is worth paying for.
  if (!has_eligible_target) return false;

  refreshOriginFromPrefs();

  // Memory pressure check: Skip publishes when there's not enough contiguous
  // heap for the publish itself (JSON buffer + esp-mqtt outbox frame + WiFi TX
  // path). Headroom only — NOT an mbedTLS preflight: persistent clients keep
  // their TLS contexts allocated for the bridge lifetime, so the old ~52 KB
  // "reserve space for reconnect" guard is obsolete post Phase 1. Publish
  // payload is capped at PUBLISH_JSON_BUFFER_SIZE (2 KB); 8 KB is a safe
  // ceiling including esp-mqtt frame overhead and transient TCP buffers.
  #ifdef ESP32
  #if defined(BOARD_HAS_PSRAM)
  static const size_t PUBLISH_SKIP_MAX_ALLOC_THRESHOLD = 16000;
  #else
  static const size_t PUBLISH_SKIP_MAX_ALLOC_THRESHOLD = 8000;
  #endif
  unsigned long now = millis();
  // Re-sample max-alloc at most once per interval and cache the verdict.
  // getMaxAllocHeap() walks the heap free-list, so it must not run per packet.
  // The previous code only advanced _last_memory_check on the healthy path, so
  // under sustained pressure the guard stayed open and it walked the heap on
  // EVERY packet — the opposite of throttling (A15). Caching the verdict keeps
  // the "skip publishes while memory is low" protection but pays for the walk
  // only once per interval; _last_memory_check is now advanced on both paths.
  if (now - _last_memory_check > 5000) {
    _last_memory_check = now;
    size_t max_alloc = ESP.getMaxAllocHeap();
    _memory_pressure = (max_alloc < PUBLISH_SKIP_MAX_ALLOC_THRESHOLD);
    if (_memory_pressure) {
      MQTT_DEBUG_PRINTLN("MQTT: memory pressure, skipping publishes (Max alloc: %d, threshold: %d, skipped: %d)",
                         (int)max_alloc, (int)PUBLISH_SKIP_MAX_ALLOC_THRESHOLD, _skipped_publishes);
    }
  }
  if (_memory_pressure) {
    _skipped_publishes++;
    return false;
  }
  #endif

  // Use pre-allocated buffer; stack fallback only when PSRAM heap alloc may be null.
#if defined(BOARD_HAS_PSRAM)
  char json_buffer_stack[PUBLISH_JSON_BUFFER_SIZE];
  char* active_buffer;
  size_t active_buffer_size;
  if (_json_scratch_buffer != nullptr) {
    active_buffer = _json_scratch_buffer;
    active_buffer_size = PUBLISH_JSON_BUFFER_SIZE;
  } else {
    active_buffer = json_buffer_stack;
    active_buffer_size = PUBLISH_JSON_BUFFER_SIZE;
  }
#else
  char* active_buffer = _json_scratch_buffer;
  const size_t active_buffer_size = PUBLISH_JSON_BUFFER_SIZE;
#endif
  char origin_id[65];

  strncpy(origin_id, _device_id, sizeof(origin_id) - 1);
  origin_id[sizeof(origin_id) - 1] = '\0';

  // Firmware rebroadcast "score" for this packet — only meaningful for RX packets
  // (depends on receive SNR). Recomputed here exactly as Dispatcher::checkRecv() does,
  // via the radio's packetScore(snr, len); NaN signals "omit" (tx, or no radio).
  // buildPacketMessage scales it x1000 to match the integer in the serial RX log.

  // Build packet message using raw radio data if provided
  int len;
  if (raw_data && raw_len > 0) {
    float score = (_radio && !is_tx) ? _radio->packetScore(snr, raw_len) : NAN;
    len = MQTTMessageBuilder::buildPacketJSONFromRaw(
      _json_scratch_doc,
      raw_data, raw_len, packet, is_tx, _origin, origin_id,
      snr, rssi, score, _timezone, active_buffer, active_buffer_size
    );
  } else if (!is_tx && _last_raw_data && _last_raw_len > 0 && (millis() - _last_raw_timestamp) < 1000) {
    float score = _radio ? _radio->packetScore(_last_snr, _last_raw_len) : NAN;
    len = MQTTMessageBuilder::buildPacketJSONFromRaw(
      _json_scratch_doc,
      _last_raw_data, _last_raw_len, packet, is_tx, _origin, origin_id,
      _last_snr, _last_rssi, score, _timezone, active_buffer, active_buffer_size
    );
  } else {
    // Reconstruct wire-format bytes from packet (same as MQTTMessageBuilder::packetToHex).
    // Reached when the queued item carried no captured raw frame, so the "raw" hex field
    // is re-serialized rather than dropped. Guarded on the packet's own length fields
    // as well as the destination, for the reasons in canSerializePacket().
    uint8_t reconstructed[MQTTMessageBuilder::WIRE_SCRATCH_SIZE];
    uint8_t rlen = 0;
    if (MQTTMessageBuilder::canSerializePacket(packet, sizeof(reconstructed))) {
      rlen = packet->writeTo(reconstructed);
    }
    if (rlen > 0) {
      float score = (_radio && !is_tx) ? _radio->packetScore(snr, rlen) : NAN;
      len = MQTTMessageBuilder::buildPacketJSONFromRaw(
        _json_scratch_doc,
        reconstructed, rlen, packet, is_tx, _origin, origin_id,
        snr, rssi, score, _timezone, active_buffer, active_buffer_size
      );
    } else {
      len = MQTTMessageBuilder::buildPacketJSON(
        _json_scratch_doc,
        packet, is_tx, _origin, origin_id, _timezone, active_buffer, active_buffer_size
      );
    }
  }

  if (len > 0) {
    bool published = false;
    char topic[128];
    for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
      // Eligibility already proved this slot's topic builds; rebuilding it is
      // one snprintf, cheaper than carrying a RUNTIME_MQTT_SLOTS x 128 topic
      // cache on the MQTT task's 8 KB stack alongside the JSON buffer.
      if ((eligible_slots & static_cast<uint8_t>(1u << i)) != 0 &&
          _slots[i].enabled && _slots[i].client && _slots[i].connected) {
        if (buildTopicForSlot(i, MSG_PACKETS, topic, sizeof(topic))) {
          if (publishToSlot(i, topic, active_buffer, (size_t)len, false)) {
            published = true;
          }
        }
      }
    }
    return published;
  } else {
    if (packet_type == 4 || packet_type == 9) {
      MQTT_DEBUG_PRINTLN("Failed to build packet JSON for type=%d (len=%d), packet not published", packet_type, len);
    }
  }
  return false;
}

bool MQTTBridge::publishRaw(mesh::Packet* packet, bool& has_eligible_target) {
  has_eligible_target = false;
  if (!packet) return false;

  const uint8_t packet_type = packet->getPayloadType();
  const uint8_t eligible_slots = eligiblePacketSlots(packet_type, MSG_RAW);
  has_eligible_target = eligible_slots != 0;
  // Filtered out everywhere, or no slot has a raw topic (MeshRank does not take
  // raw): intentionally complete, and no JSON is built.
  if (!has_eligible_target) return false;

  refreshOriginFromPrefs();

#if defined(BOARD_HAS_PSRAM)
  char json_buffer_stack[PUBLISH_JSON_BUFFER_SIZE];
  char* active_buffer;
  size_t active_buffer_size;
  if (_json_scratch_buffer != nullptr) {
    active_buffer = _json_scratch_buffer;
    active_buffer_size = PUBLISH_JSON_BUFFER_SIZE;
  } else {
    active_buffer = json_buffer_stack;
    active_buffer_size = PUBLISH_JSON_BUFFER_SIZE;
  }
#else
  char* active_buffer = _json_scratch_buffer;
  const size_t active_buffer_size = PUBLISH_JSON_BUFFER_SIZE;
#endif
  char origin_id[65];

  strncpy(origin_id, _device_id, sizeof(origin_id) - 1);
  origin_id[sizeof(origin_id) - 1] = '\0';

  int len = MQTTMessageBuilder::buildRawJSON(
    _json_scratch_doc,
    packet, _origin, origin_id, _timezone, active_buffer, active_buffer_size
  );

  if (len > 0) {
    bool published = false;
    char topic[128];
    for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
      if ((eligible_slots & static_cast<uint8_t>(1u << i)) != 0 &&
          _slots[i].enabled && _slots[i].client && _slots[i].connected) {
        if (buildTopicForSlot(i, MSG_RAW, topic, sizeof(topic))) {
          if (publishToSlot(i, topic, active_buffer, (size_t)len, false)) {
            published = true;
          }
        }
      }
    }
    return published;
  }
  return false;
}

#if defined(WITH_MQTT_NEIGHBORS)
// ---------------------------------------------------------------------------
// Periodic neighbors publication
// ---------------------------------------------------------------------------

void MQTTBridge::setNeighborsSchedule(NeighborsPhase phase, uint32_t secs_until_next) {
  _neighbors_phase.store((uint8_t)phase, std::memory_order_relaxed);
  _neighbors_secs_until_next.store(secs_until_next, std::memory_order_relaxed);
}

void MQTTBridge::requestPublishNeighbors(const char* json, size_t len) {
  if (!_neighbors_json_buffer || !json || len == 0) return;
  // Drop a new snapshot while one is still being published (Core 0 clears the
  // flag when done). Acquire pairs with the task loop's release store.
  if (_neighbors_publish_pending.load(std::memory_order_acquire)) return;
  if (len >= NEIGHBORS_JSON_BUFFER_SIZE) {
    len = NEIGHBORS_JSON_BUFFER_SIZE - 1;
  }
  memcpy(_neighbors_json_buffer, json, len);
  _neighbors_json_buffer[len] = '\0';
  _neighbors_publish_len = len;
  _neighbors_publish_pending.store(true, std::memory_order_release);
}

bool MQTTBridge::publishNeighbors() {
  if (!_neighbors_json_buffer || _neighbors_publish_len == 0) return false;
  if (!_cached_has_connected_slots) return false;

  refreshOriginFromPrefs();

  bool published = false;
  char topic[128];
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].enabled && _slots[i].client && _slots[i].connected) {
      // Slots that cannot form a neighbors topic are skipped here.
      if (buildTopicForSlot(i, MSG_NEIGHBORS, topic, sizeof(topic))) {
        // Neighbor snapshots are periodically refreshed. Publish synchronously
        // at QoS 0 to avoid the QoS 1 outbox, retaining where the broker allows.
        bool use_retain = _slots[i].preset ? _slots[i].preset->allow_retain : false;
        if (publishToSlot(i, topic, _neighbors_json_buffer, _neighbors_publish_len, use_retain, 0)) {
          published = true;
        }
      }
    }
  }
  return published;
}
#endif  // WITH_MQTT_NEIGHBORS

// ---------------------------------------------------------------------------
// Queue management
// ---------------------------------------------------------------------------

void MQTTBridge::queuePacket(mesh::Packet* packet, bool is_tx) {
  #ifdef ESP_PLATFORM
  // Use FreeRTOS queue for thread-safe operation
  if (_packet_queue_handle == nullptr) {
    return;
  }

  QueuedPacket queued;
  memset(&queued, 0, sizeof(QueuedPacket));

  queued.packet_copy = *packet;  // full value copy — safe from Dispatcher free
  queued.timestamp = millis();
  queued.is_tx = is_tx;
  queued.snr = 0.0f;
  queued.rssi = 0.0f;

  // Consume staged raw data (written by storeRawRadioData() on Core 1, same call sequence).
  // No mutex needed — both sites run on Core 1 before xQueueSend() crosses the core boundary.
  if (!is_tx && _staged_raw_valid) {
    if (_staged_raw_len <= (int)sizeof(queued.raw_data)) {
      memcpy(queued.raw_data, _staged_raw, _staged_raw_len);
      queued.raw_len    = (uint8_t)_staged_raw_len;
      queued.has_raw_data = true;
    }
    queued.snr          = _staged_snr;
    queued.rssi         = _staged_rssi;
    _staged_raw_valid   = false;  // consumed; cleared before xQueueSend
  } else if (is_tx) {
    // For TX packets, snapshot the exact serialized wire bytes at enqueue time so
    // publishPacket() can use the direct raw-data path (not reconstruction fallback).
    uint8_t tx_len = packet->writeTo(queued.raw_data);
    if (tx_len > 0) {
      queued.raw_len = tx_len;
      queued.has_raw_data = true;
    }
  }

  // Try to send to queue (non-blocking)
  if (xQueueSend(_packet_queue_handle, &queued, 0) != pdTRUE) {
    const MQTTPacketQueuePolicy::EnqueueAction action =
        MQTTPacketQueuePolicy::enqueueAction(
            static_cast<size_t>(uxQueueMessagesWaiting(_packet_queue_handle)),
            static_cast<size_t>(MAX_QUEUE_SIZE));
    QueuedPacket oldest;
    if (action == MQTTPacketQueuePolicy::EnqueueAction::EvictOldestThenEnqueue &&
        xQueueReceive(_packet_queue_handle, &oldest, 0) == pdTRUE) {
      MQTT_DEBUG_PRINTLN("Queue full, dropping oldest packet reference");
    } else if (action == MQTTPacketQueuePolicy::EnqueueAction::Reject) {
      MQTT_DEBUG_PRINTLN("Queue has no capacity");
      return;
    } else if (action == MQTTPacketQueuePolicy::EnqueueAction::EvictOldestThenEnqueue) {
      MQTT_DEBUG_PRINTLN("Queue full and cannot remove oldest packet");
      return;
    }
    // If the consumer made room after the failed send, retry without evicting.
    if (xQueueSend(_packet_queue_handle, &queued, 0) != pdTRUE) {
      MQTT_DEBUG_PRINTLN("Failed to queue packet after overflow handling");
      return;
    }
  }

  UBaseType_t queue_messages = uxQueueMessagesWaiting(_packet_queue_handle);
  _queue_count = queue_messages;
  #else
  // Non-ESP32: Use circular buffer
  const MQTTPacketQueuePolicy::EnqueueAction action =
      MQTTPacketQueuePolicy::enqueueAction(
          static_cast<size_t>(_queue_count), static_cast<size_t>(MAX_QUEUE_SIZE));
  if (action == MQTTPacketQueuePolicy::EnqueueAction::EvictOldestThenEnqueue) {
    MQTT_DEBUG_PRINTLN("Queue full, dropping oldest packet (queue size: %d)", _queue_count);
    dequeuePacket();
  } else if (action == MQTTPacketQueuePolicy::EnqueueAction::Reject) {
    return;
  }

  QueuedPacket& queued = _packet_queue[_queue_tail];
  memset(&queued, 0, sizeof(QueuedPacket));

  queued.packet_copy = *packet;  // full value copy — safe from Dispatcher free
  queued.timestamp = millis();
  queued.is_tx = is_tx;
  queued.snr = 0.0f;
  queued.rssi = 0.0f;

  if (!is_tx && _staged_raw_valid) {
    if (_staged_raw_len <= (int)sizeof(queued.raw_data)) {
      memcpy(queued.raw_data, _staged_raw, _staged_raw_len);
      queued.raw_len    = (uint8_t)_staged_raw_len;
      queued.has_raw_data = true;
    }
    queued.snr          = _staged_snr;
    queued.rssi         = _staged_rssi;
    _staged_raw_valid   = false;
  } else if (is_tx) {
    // Mirror ESP32 path: persist serialized TX bytes directly in queue entry.
    uint8_t tx_len = packet->writeTo(queued.raw_data);
    if (tx_len > 0) {
      queued.raw_len = tx_len;
      queued.has_raw_data = true;
    }
  }

  _queue_tail = (_queue_tail + 1) % MAX_QUEUE_SIZE;
  _queue_count++;
  #endif
}

void MQTTBridge::dequeuePacket() {
  #ifdef ESP_PLATFORM
  // On ESP32, dequeuePacket() is not used - we use FreeRTOS queue operations directly
  return;
  #else
  if (_queue_count == 0) return;

  QueuedPacket& dequeued = _packet_queue[_queue_head];
  memset(&dequeued, 0, sizeof(QueuedPacket));
  dequeued.has_raw_data = false;

  _queue_head = (_queue_head + 1) % MAX_QUEUE_SIZE;
  _queue_count--;
  #endif
}

// ---------------------------------------------------------------------------
// Raw radio data storage
// ---------------------------------------------------------------------------

void MQTTBridge::storeRawRadioData(const uint8_t* raw_data, int len, float snr, float rssi) {
  // Writes into the Core 1-only staging area. No mutex needed: this function and
  // queuePacket() are both called from Core 1 in guaranteed sequence for each packet.
  if (len > 0 && len <= (int)LAST_RAW_DATA_SIZE) {
    memcpy(_staged_raw, raw_data, len);
    _staged_raw_len   = len;
    _staged_snr       = snr;
    _staged_rssi      = rssi;
    _staged_raw_valid = true;
    MQTT_DEBUG_PRINTLN("Stored raw radio data: %d bytes, SNR=%.1f, RSSI=%.1f", len, snr, rssi);
  }
}

// ---------------------------------------------------------------------------
// NTP time sync
// ---------------------------------------------------------------------------

void MQTTBridge::refreshNTP() {
  // Lightweight periodic refresh: just restart SNTP which runs async in the background.
  // No blocking DNS, no UDP sockets, no retry loops on the MQTT task loop.
  // The heavy syncTimeWithNTP() is only used for initial sync and WiFi reconnect recovery.
  configTime(0, 0, effectiveNtpPrimary(_obs));
  _last_ntp_sync = millis();
  MQTT_DEBUG_PRINTLN("NTP refresh triggered (async SNTP)");
}

bool MQTTBridge::syncTimeWithNTP(bool force, bool primary_only) {
  if (!WiFi.isConnected()) {
    MQTT_DEBUG_PRINTLN("Cannot sync time - WiFi not connected");
    return false;
  }

  unsigned long now = millis();
  if (!force && _ntp_synced && (now - _last_ntp_sync) < 5000) {
    return false;
  }

  static bool sync_in_progress = false;
  if (sync_in_progress) {
    return false;
  }
  sync_in_progress = true;

  MQTT_DEBUG_PRINTLN("Syncing time with NTP...");

  const char* servers[kMaxNtpServers];
  int server_count = 0;
  if (primary_only) {
    // Validation path (e.g. set mqtt.ntp): test only the configured primary so a
    // typo fails fast instead of walking the entire fallback list.
    servers[0] = effectiveNtpPrimary(_obs);
    server_count = 1;
  } else {
    fillNtpServerList(_obs, servers, server_count);
  }

  bool ntp_ok = false;
  unsigned long epochTime = 0;
  const unsigned long kMinValidEpoch = 1767225600;  // 2026-01-01 00:00:00 UTC
  const char* ntp_server_used = nullptr;

  _ntp_client.begin();
  const int kMaxNtpRetriesPerServer = 2;
  for (int s = 0; s < server_count && !ntp_ok; s++) {
    const char* server = servers[s];
    _ntp_client.setPoolServerName(server);

    #ifdef ESP_PLATFORM
    IPAddress resolved_ip;
    if (!WiFi.hostByName(server, resolved_ip)) {
      MQTT_DEBUG_PRINTLN("WARNING: DNS resolution failed for %s - NTP sync may fail", server);
    }
    #endif

    for (int attempt = 1; attempt <= kMaxNtpRetriesPerServer && !ntp_ok; attempt++) {
      if (attempt > 1) {
        MQTT_DEBUG_PRINTLN("NTP retry %d/%d on %s...", attempt, kMaxNtpRetriesPerServer, server);
        delay(1000);
      }
      if (_ntp_client.forceUpdate()) {
        epochTime = _ntp_client.getEpochTime();
        if (epochTime >= kMinValidEpoch) {
          ntp_ok = true;
          ntp_server_used = server;
        }
      }
    }
  }
  _ntp_client.end();

  // Fallback: use ESP32 built-in SNTP (configTime) when NTPClient fails
  #ifdef ESP_PLATFORM
  if (!ntp_ok) {
    MQTT_DEBUG_PRINTLN("NTP client failed, trying SNTP fallback...");
    for (int s = 0; s < server_count && !ntp_ok; s++) {
      const char* server = servers[s];
      MQTT_DEBUG_PRINTLN("SNTP fallback trying %s...", server);
      configTime(0, 0, server);
      for (int i = 0; i < 20; i++) {
        delay(500);
        epochTime = (unsigned long)time(nullptr);
        if (epochTime >= kMinValidEpoch) {
          ntp_ok = true;
          ntp_server_used = server;
          MQTT_DEBUG_PRINTLN("SNTP fallback succeeded on %s: %lu", server, epochTime);
          break;
        }
      }
    }
  }
  #endif

  if (ntp_ok && ntp_server_used) {
    configTime(0, 0, ntp_server_used);

    if (_rtc) {
      _rtc->setCurrentTime(epochTime);
    }

    bool was_ntp_synced = _ntp_synced;
    _ntp_synced = true;
    _last_ntp_sync = millis();
    sync_in_progress = false;

    MQTT_DEBUG_PRINTLN("Time synced: %lu (via %s)", epochTime, ntp_server_used);

    // If slots are already set up and the time jumped significantly (e.g., SNTP
    // initially returned stale RTC time, then a later sync corrected it), tear down
    // and re-setup all JWT-authenticated slots so they get fresh tokens.
    if (_slots_setup_done && was_ntp_synced) {
      unsigned long current_time = (unsigned long)time(nullptr);
      // Every slot, not _max_active_slots: that is a count of positions, never an
      // index bound. Which indices hold those positions is not contiguous — a slot can
      // fail isSlotReady() or its setup and be passed over, leaving a higher index
      // activated — so bounding by the cap silently skipped an activated slot and left
      // it holding a JWT issued against the pre-correction clock. The guard below
      // already excludes disabled, non-JWT, and clientless slots.
      for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
        bool slot_jwt = (_slots[i].preset && _slots[i].preset->auth_type == MQTT_AUTH_JWT) ||
                        (!_slots[i].preset && _slots[i].audience[0] != '\0');
        if (_slots[i].enabled && slot_jwt && _slots[i].client) {
          // Token created before NTP corrected the clock — refresh credentials
          // in place and reconnect the persistent client. No teardown needed.
          if (_slots[i].token_expires_at > 0 && current_time > _slots[i].token_expires_at) {
            MQTT_DEBUG_PRINTLN("MQTT%d token stale after time correction, re-creating", i + 1);
            if (createSlotAuthToken(i)) {
              _slots[i].client->setCredentials(_jwt_username, _slots[i].auth_token);
            }
            _slots[i].client->reconnect();
          }
        }
      }
    }

    // Set timezone from string (with DST support) — only if changed.
    // Reuses the inline _timezone_storage via setRules() instead of
    // deleting/newing a Timezone, which was a per-change heap alloc pair.
    static char last_timezone[64] = "";
    if (strcmp(_obs->timezone_string, last_timezone) != 0) {
      TimeChangeRule dst_rule, std_rule;
      if (!timezoneRulesFromString(_obs->timezone_string, dst_rule, std_rule)) {
        TimeChangeRule utc = {"UTC", Last, Sun, Mar, 0, 0};
        dst_rule = utc;
        std_rule = utc;
      }
      _timezone_storage.setRules(dst_rule, std_rule);
      strncpy(last_timezone, _obs->timezone_string, sizeof(last_timezone) - 1);
      last_timezone[sizeof(last_timezone) - 1] = '\0';
    }

    return true;
  }

  MQTT_DEBUG_PRINTLN("NTP sync failed");
  sync_in_progress = false;
  return false;
}

bool MQTTBridge::requestForcedNtpSync(uint32_t timeout_ms) {
  if (!isRunning()) return false;

  // Publish the request to the MQTT task. Clear the completion flags before
  // raising _ntp_force_requested so the task can't observe a stale result.
  _ntp_force_done = false;
  _ntp_force_result = false;
  _ntp_force_requested = true;

  // Fire-and-forget: callers on the Arduino loop task (web config batch, and
  // the CLI which shares that task) must not block up to 30 s polling the MQTT
  // task — that stalls mesh/radio forwarding, portal DNS, and reboot timers.
  // The task still performs the sync; the result is observable via
  // `get mqtt.ntp.diag`. Blocking callers pass a non-zero timeout.
  if (timeout_ms == 0) return true;

  unsigned long start = millis();
  while (!_ntp_force_done) {
    if (millis() - start >= timeout_ms) {
      MQTT_DEBUG_PRINTLN("Forced NTP sync timed out waiting for MQTT task");
      return false;  // task still running; result is ignored when it eventually completes
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  return _ntp_force_result;
}

// Runs on the MQTT task (Core 0). Probes every configured NTP server for connectivity
// and records the time each reports. Deliberately does NOT call configTime() or update
// the RTC — this is a read-only diagnostic and must leave the system clock untouched.
void MQTTBridge::runNtpDiagProbe() {
  const char* servers[kMaxNtpServers];
  int count = 0;
  fillNtpServerList(_obs, servers, count);

  _ntp_client.begin();
  for (int i = 0; i < count; i++) {
    _ntp_client.setPoolServerName(servers[i]);
    bool ok = _ntp_client.forceUpdate();
    NtpDiagResult& r = _ntp_diag_results[i];
    strncpy(r.server, servers[i], sizeof(r.server) - 1);
    r.server[sizeof(r.server) - 1] = '\0';
    r.ok = ok;
    r.epoch = ok ? (uint32_t)_ntp_client.getEpochTime() : 0;
  }
  _ntp_diag_count = count;
}

bool MQTTBridge::ntpDiag(char* reply, size_t reply_size, bool verbose) {
  if (!isRunning() || reply == nullptr || reply_size == 0) return false;

  // Marshal the probe onto the MQTT task (Core 0); clear the completion flag first.
  _ntp_diag_done = false;
  _ntp_diag_requested = true;

  unsigned long start = millis();
  while (!_ntp_diag_done) {
    if (millis() - start >= 30000) {
      snprintf(reply, reply_size, "Error: NTP diag timed out");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  int ok_count = 0;
  for (int i = 0; i < _ntp_diag_count; i++) {
    if (_ntp_diag_results[i].ok) ok_count++;
  }

  if (verbose) {
    // Detailed table to the serial console; reply carries a short summary (the operator
    // sees the table above the "-> <reply>" line). Mirrors the dumpLogFile() convention.
    Serial.printf("NTP diag - %d server(s):\r\n", _ntp_diag_count);
    for (int i = 0; i < _ntp_diag_count; i++) {
      const NtpDiagResult& r = _ntp_diag_results[i];
      if (r.ok) {
        time_t t = (time_t)r.epoch;
        struct tm* tmv = gmtime(&t);
        Serial.printf("  %-20s OK    %04d-%02d-%02d %02d:%02d:%02d UTC\r\n",
                      r.server, tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                      tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
      } else {
        Serial.printf("  %-20s FAIL\r\n", r.server);
      }
    }
    snprintf(reply, reply_size, "> NTP diag: %d/%d OK (see console)", ok_count, _ntp_diag_count);
  } else {
    // Compact "<server> ok|fail" list for LoRa, bounded to reply_size.
    size_t used = 0;
    reply[0] = '\0';
    for (int i = 0; i < _ntp_diag_count; i++) {
      const NtpDiagResult& r = _ntp_diag_results[i];
      int n = snprintf(reply + used, reply_size - used, "%s%s %s",
                       used ? "\n" : "", r.server, r.ok ? "ok" : "fail");
      if (n < 0 || (size_t)n >= reply_size - used) {
        reply[used] = '\0';  // out of room — truncate cleanly
        break;
      }
      used += (size_t)n;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Timezone helper
// ---------------------------------------------------------------------------

// Populates dst_out and std_out with the DST/standard TimeChangeRules for the
// given timezone string. Returns true on match, false on unknown strings. Zero
// heap allocation — the caller then passes these into Timezone::setRules() on
// an existing Timezone object.
bool MQTTBridge::timezoneRulesFromString(const char* tz_string, TimeChangeRule& dst_out, TimeChangeRule& std_out) {
  // GCC refuses to implicitly build a TimeChangeRule temporary from a bare
  // braced-init-list on the right-hand side of operator= (the aggregate has a
  // char[6] member). Name the type explicitly so a proper temporary is formed.
  // North America
  if (strcmp(tz_string, "America/Los_Angeles") == 0 || strcmp(tz_string, "America/Vancouver") == 0) {
    std_out = TimeChangeRule{"PST", First, Sun, Nov, 2, -480};
    dst_out = TimeChangeRule{"PDT", Second, Sun, Mar, 2, -420};
    return true;
  } else if (strcmp(tz_string, "America/Denver") == 0) {
    std_out = TimeChangeRule{"MST", First, Sun, Nov, 2, -420};
    dst_out = TimeChangeRule{"MDT", Second, Sun, Mar, 2, -360};
    return true;
  } else if (strcmp(tz_string, "America/Chicago") == 0) {
    std_out = TimeChangeRule{"CST", First, Sun, Nov, 2, -360};
    dst_out = TimeChangeRule{"CDT", Second, Sun, Mar, 2, -300};
    return true;
  } else if (strcmp(tz_string, "America/New_York") == 0 || strcmp(tz_string, "America/Toronto") == 0) {
    std_out = TimeChangeRule{"EST", First, Sun, Nov, 2, -300};
    dst_out = TimeChangeRule{"EDT", Second, Sun, Mar, 2, -240};
    return true;
  } else if (strcmp(tz_string, "America/Anchorage") == 0) {
    std_out = TimeChangeRule{"AKST", First, Sun, Nov, 2, -540};
    dst_out = TimeChangeRule{"AKDT", Second, Sun, Mar, 2, -480};
    return true;
  } else if (strcmp(tz_string, "Pacific/Honolulu") == 0) {
    TimeChangeRule hst = {"HST", Last, Sun, Oct, 2, -600};
    dst_out = hst; std_out = hst;
    return true;

  // Europe
  } else if (strcmp(tz_string, "Europe/London") == 0) {
    std_out = TimeChangeRule{"GMT", Last, Sun, Oct, 2, 0};
    dst_out = TimeChangeRule{"BST", Last, Sun, Mar, 1, 60};
    return true;
  } else if (strcmp(tz_string, "Europe/Paris") == 0 || strcmp(tz_string, "Europe/Berlin") == 0) {
    std_out = TimeChangeRule{"CET", Last, Sun, Oct, 3, 60};
    dst_out = TimeChangeRule{"CEST", Last, Sun, Mar, 2, 120};
    return true;
  } else if (strcmp(tz_string, "Europe/Moscow") == 0) {
    TimeChangeRule msk = {"MSK", Last, Sun, Oct, 3, 180};
    dst_out = msk; std_out = msk;
    return true;

  // Asia
  } else if (strcmp(tz_string, "Asia/Tokyo") == 0) {
    TimeChangeRule jst = {"JST", Last, Sun, Oct, 2, 540};
    dst_out = jst; std_out = jst;
    return true;
  } else if (strcmp(tz_string, "Asia/Shanghai") == 0 || strcmp(tz_string, "Asia/Hong_Kong") == 0) {
    TimeChangeRule cst = {"CST", Last, Sun, Oct, 2, 480};
    dst_out = cst; std_out = cst;
    return true;
  } else if (strcmp(tz_string, "Asia/Kolkata") == 0) {
    TimeChangeRule ist = {"IST", Last, Sun, Oct, 2, 330};
    dst_out = ist; std_out = ist;
    return true;
  } else if (strcmp(tz_string, "Asia/Dubai") == 0) {
    TimeChangeRule gst = {"GST", Last, Sun, Oct, 2, 240};
    dst_out = gst; std_out = gst;
    return true;

  // Australia
  } else if (strcmp(tz_string, "Australia/Sydney") == 0 || strcmp(tz_string, "Australia/Melbourne") == 0) {
    std_out = TimeChangeRule{"AEST", First, Sun, Apr, 3, 600};
    dst_out = TimeChangeRule{"AEDT", First, Sun, Oct, 2, 660};
    return true;
  } else if (strcmp(tz_string, "Australia/Perth") == 0) {
    TimeChangeRule awst = {"AWST", Last, Sun, Oct, 2, 480};
    dst_out = awst; std_out = awst;
    return true;

  // Timezone abbreviations (with DST handling)
  } else if (strcmp(tz_string, "PDT") == 0 || strcmp(tz_string, "PST") == 0) {
    std_out = TimeChangeRule{"PST", First, Sun, Nov, 2, -480};
    dst_out = TimeChangeRule{"PDT", Second, Sun, Mar, 2, -420};
    return true;
  } else if (strcmp(tz_string, "MDT") == 0 || strcmp(tz_string, "MST") == 0) {
    std_out = TimeChangeRule{"MST", First, Sun, Nov, 2, -420};
    dst_out = TimeChangeRule{"MDT", Second, Sun, Mar, 2, -360};
    return true;
  } else if (strcmp(tz_string, "CDT") == 0 || strcmp(tz_string, "CST") == 0) {
    std_out = TimeChangeRule{"CST", First, Sun, Nov, 2, -360};
    dst_out = TimeChangeRule{"CDT", Second, Sun, Mar, 2, -300};
    return true;
  } else if (strcmp(tz_string, "EDT") == 0 || strcmp(tz_string, "EST") == 0) {
    std_out = TimeChangeRule{"EST", First, Sun, Nov, 2, -300};
    dst_out = TimeChangeRule{"EDT", Second, Sun, Mar, 2, -240};
    return true;
  } else if (strcmp(tz_string, "BST") == 0 || strcmp(tz_string, "GMT") == 0) {
    std_out = TimeChangeRule{"GMT", Last, Sun, Oct, 2, 0};
    dst_out = TimeChangeRule{"BST", Last, Sun, Mar, 1, 60};
    return true;
  } else if (strcmp(tz_string, "CEST") == 0 || strcmp(tz_string, "CET") == 0) {
    std_out = TimeChangeRule{"CET", Last, Sun, Oct, 3, 60};
    dst_out = TimeChangeRule{"CEST", Last, Sun, Mar, 2, 120};
    return true;

  // UTC and simple offsets
  } else if (strcmp(tz_string, "UTC") == 0) {
    TimeChangeRule utc = {"UTC", Last, Sun, Mar, 0, 0};
    dst_out = utc; std_out = utc;
    return true;
  } else if (strncmp(tz_string, "UTC", 3) == 0) {
    int offset = atoi(tz_string + 3);
    TimeChangeRule utc_offset = {"UTC", Last, Sun, Mar, 0, offset * 60};
    dst_out = utc_offset; std_out = utc_offset;
    return true;
  } else if (strncmp(tz_string, "GMT", 3) == 0) {
    int offset = atoi(tz_string + 3);
    TimeChangeRule gmt_offset = {"GMT", Last, Sun, Mar, 0, offset * 60};
    dst_out = gmt_offset; std_out = gmt_offset;
    return true;
  } else if (tz_string[0] == '+' || tz_string[0] == '-') {
    int offset = atoi(tz_string);
    TimeChangeRule offset_tz = {"TZ", Last, Sun, Mar, 0, offset * 60};
    dst_out = offset_tz; std_out = offset_tz;
    return true;
  } else {
    MQTT_DEBUG_PRINTLN("Unknown timezone: %s", tz_string);
    return false;
  }
}

// ---------------------------------------------------------------------------
// Utility methods
// ---------------------------------------------------------------------------

void MQTTBridge::getClientVersion(char* buffer, size_t buffer_size) const {
  if (!buffer || buffer_size == 0) {
    return;
  }
  snprintf(buffer, buffer_size, "meshcore/%s", _firmware_version);
}

void MQTTBridge::optimizeMqttClientConfig(PsychicMqttClient* client, bool needs_large_buffer) {
  if (!client) return;

  // Cloudflare closes WebSocket connections after 100s idle (non-configurable).
#if defined(BOARD_HAS_PSRAM)
  client->setKeepAlive(45);
#else
  // Non-PSRAM: use a longer keepalive to reduce TLS teardown/reconnect churn.
  // 75s is safe behind Cloudflare (100s idle timeout, 25s margin).
  client->setKeepAlive(75);
#endif

  // QoS 1 retransmit timeout for unacked PUBLISHes (status messages). esp-mqtt's
  // 1000 ms default resends a byte-identical duplicate every second whenever the
  // broker's PUBACK takes >1s — on a congested or recovering uplink this floods
  // subscribers with exact copies of one /status message (observed 6 copies ~1s
  // apart after an ISP outage; brokers may drop the session as spam). 15s allows
  // one retry before the outbox entry expires (esp-mqtt outbox expiry is 30s),
  // preserving at-least-once delivery while capping duplicates at one.
  client->setMessageRetransmitTimeout(15000);

  // Buffer sizing: 896 is the minimum safe size for JWT clients (CONNECT + 768-byte JWT).
  // On PSRAM boards, use a uniform size to reduce fragmentation from mixed allocations.
  // On non-PSRAM boards, use smaller buffers for non-JWT slots to reduce heap usage and
  // leave smaller holes during teardown/recreate cycles.
#if defined(BOARD_HAS_PSRAM)
  static const int MQTT_CLIENT_BUFFER_SIZE = 896;
#else
  const int MQTT_CLIENT_BUFFER_SIZE = needs_large_buffer ? 896 : 512;
#endif

  client->setBufferSize(MQTT_CLIENT_BUFFER_SIZE);

  // Bound how long a synchronous QoS0 publish (see publishToSlot) can block the MQTT
  // task on a stalled/half-open socket before esp-mqtt aborts the write. Default is 10s;
  // 2.5s lets a first stall resolve fast (write fails → slot flips to disconnected →
  // subsequent packets skip it) without holding up publishing to the other slots. Mesh
  // RX (Core 1) and the WiFi/TCP stack are unaffected by this block regardless.
  client->setNetworkTimeout(2500);

  // Dormant safety net: cap the esp-mqtt outbox for any residual async QoS0 path. QoS0
  // packets now publish synchronously (store=false, no outbox), so this normally never
  // engages, but it bounds internal-heap growth if a QoS0 message ever takes the async
  // path. Non-PSRAM (outbox on internal heap) gets the tighter cap.
#if defined(BOARD_HAS_PSRAM)
  client->setOutboxLimit(16384);
#else
  client->setOutboxLimit(8192);
#endif

  // Access ESP-IDF config to optimize additional settings
  esp_mqtt_client_config_t* config = client->getMqttConfig();
  if (config) {
    #if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
      // Keep the output buffer (used to build the CONNECT/PUBLISH frames) in lockstep
      // with the input buffer. setBufferSize() above only sets buffer.size, so out_size
      // must be set here. The previous conditional only ever shrank out_size or set it
      // from 0 — when a slot was reconfigured from a small non-JWT buffer (512) up to
      // the JWT buffer (896), out_size stayed at 512 and the JWT CONNECT frame (username
      // + ~537-768B token) overflowed it, producing esp-mqtt "Connect message cannot be
      // created". Always matching MQTT_CLIENT_BUFFER_SIZE fixes the grow case.
      config->buffer.out_size = MQTT_CLIENT_BUFFER_SIZE;
    #endif
  }
}

void MQTTBridge::logMemoryStatus() {
  // QoS0 packets now publish synchronously, so the outbox stays ~0 and is only a sanity
  // check (a non-zero total would mean the QoS1 status path is backing up or the dormant
  // async cap engaged). The live signal is per-slot publish health: ok = cumulative
  // accepted writes, err = cumulative failures (socket error / network_timeout on a
  // stalled link). A rising err on a slot means that broker's uplink is dropping packets;
  // ok climbing with err flat is healthy delivery.
  char pub_detail[200];
  size_t pos = 0;
  size_t outbox_total = 0;
  pub_detail[0] = '\0';
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].client) {
      outbox_total += _slots[i].client->getOutboxSize();
      if (_slots[i].enabled) {
        pos += snprintf(pub_detail + pos, sizeof(pub_detail) - pos, "%ss%d=%lu/%lu",
                        pos ? " " : "", i + 1,
                        _slots[i].client->getPublishOk(),
                        _slots[i].client->getPublishErr());
        if (pos >= sizeof(pub_detail)) break;
      }
    }
  }
  MQTT_DEBUG_PRINTLN("Memory: Free=%d, Max=%d, Queue=%d/%d, Outbox=%u | pub(ok/err) %s",
                     ESP.getFreeHeap(), ESP.getMaxAllocHeap(), _queue_count, MAX_QUEUE_SIZE,
                     (unsigned)outbox_total, pub_detail);
}

// ---------------------------------------------------------------------------
// Setters and accessors
// ---------------------------------------------------------------------------

void MQTTBridge::setOrigin(const char* origin) {
  strncpy(_origin, origin, sizeof(_origin) - 1);
  _origin[sizeof(_origin) - 1] = '\0';
}

void MQTTBridge::setIATA(const char* iata) {
  strncpy(_iata, iata, sizeof(_iata) - 1);
  _iata[sizeof(_iata) - 1] = '\0';
  for (int i = 0; _iata[i]; i++) {
    _iata[i] = toupper(_iata[i]);
  }
}

void MQTTBridge::setDeviceID(const char* device_id) {
  strncpy(_device_id, device_id, sizeof(_device_id) - 1);
  _device_id[sizeof(_device_id) - 1] = '\0';
  MQTT_DEBUG_PRINTLN("Device ID set to: %s", _device_id);
}

void MQTTBridge::setFirmwareVersion(const char* firmware_version) {
  strncpy(_firmware_version, firmware_version, sizeof(_firmware_version) - 1);
  _firmware_version[sizeof(_firmware_version) - 1] = '\0';
}

void MQTTBridge::setBoardModel(const char* board_model) {
  strncpy(_board_model, board_model, sizeof(_board_model) - 1);
  _board_model[sizeof(_board_model) - 1] = '\0';
}

void MQTTBridge::setBuildDate(const char* build_date) {
  strncpy(_build_date, build_date, sizeof(_build_date) - 1);
  _build_date[sizeof(_build_date) - 1] = '\0';
}

void MQTTBridge::setMessageTypes(bool status, bool packets, bool raw) {
  _status_enabled = status;
  _packets_enabled = packets;
  _raw_enabled = raw;
}

int MQTTBridge::getConnectedBrokers() const {
  int count = 0;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    if (_slots[i].enabled && _slots[i].connected) {
      count++;
    }
  }
  return count;
}

int MQTTBridge::getQueueSize() const {
  #ifdef ESP_PLATFORM
  if (_packet_queue_handle != nullptr) {
    return uxQueueMessagesWaiting(_packet_queue_handle);
  }
  return 0;
  #else
  return _queue_count;
  #endif
}

void MQTTBridge::setStatsSources(mesh::Dispatcher* dispatcher, mesh::Radio* radio,
                                  mesh::MainBoard* board, mesh::MillisecondClock* ms) {
  _dispatcher = dispatcher;
  _radio = radio;
  _board = board;
  _ms = ms;
}

#endif
