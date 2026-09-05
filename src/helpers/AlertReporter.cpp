#include "AlertReporter.h"

#include <Utils.h>
#include <Packet.h>
#include <string.h>
#include <stdio.h>

// Header layout for PAYLOAD_TYPE_GRP_TXT before encryption:
//   [0..3] timestamp (uint32_t LE) — also helps make packet_hash unique
//   [4]    TXT_TYPE_PLAIN
//   [5..]  "<sender>: <text>"  (null-terminated by sender for legacy parsers)
#ifndef MAX_ALERT_TEXT_LEN
// Conservative ceiling: matches BaseChatMesh::MAX_TEXT_LEN (10 * 16 = 160) and
// stays under MAX_PACKET_PAYLOAD - 4(timestamp) - 1(type) - CIPHER_MAC_SIZE - 1.
#define MAX_ALERT_TEXT_LEN 160
#endif

#ifndef ALERT_TXT_TYPE_PLAIN
#define ALERT_TXT_TYPE_PLAIN 0
#endif

#ifdef MQTT_DEBUG
#include <Arduino.h>
#define ALERT_DEBUG_PRINTLN(...) Serial.printf("Alert: " __VA_ARGS__); Serial.println()
#else
#define ALERT_DEBUG_PRINTLN(...) do {} while (0)
#endif

#ifdef WITH_MQTT_BRIDGE
AlertReporter::AlertReporter()
    : _prefs(nullptr), _obs(nullptr), _mesh(nullptr), _callbacks(nullptr),
#ifdef WITH_MQTT_BRIDGE
      _bridge(nullptr),
#endif
      _next_check_ms(0) {
#ifdef WITH_MQTT_BRIDGE
  memset(&_wifi, 0, sizeof(_wifi));
  memset(&_mqtt, 0, sizeof(_mqtt));
#endif
}

void AlertReporter::begin(NodePrefs* prefs, MQTTPrefs* obs, mesh::Mesh* mesh, CommonCLICallbacks* callbacks) {
  _prefs = prefs;
  _obs = obs;
  _mesh = mesh;
  _callbacks = callbacks;
  onConfigChanged();
}

void AlertReporter::setBridge(MQTTBridge* bridge) {
  _bridge = bridge;
}
#endif // WITH_MQTT_BRIDGE (AlertReporter methods, part 1)

// Channels banned as fault-alert destinations. Fault alerts are noisy
// operator-infrastructure messages; routing them to community channels would
// flood every nearby companion app (and amplify via well-known auto-responder
// bots), so the firmware refuses these keys at both CLI set-time and at
// runtime in resolveChannel.
//
// Provenance for each row can be re-derived with:
//   printf '#name' | openssl dgst -sha256 | cut -c1-32
// or for the Public PSK:
//   echo 'izOH6cXN6mrJ5e26oRXNcg==' | base64 -d | xxd -p -c 16
//
// To ban an additional channel: append one new row; no other code changes
// required. Both the table entries and `alert_psk_hex` are 32 lowercase hex
// chars (16-byte secret), so the matcher is a direct strcmp.
struct BannedAlertChannel {
  const char* label;
  const char* secret_hex;  // 32 lowercase hex chars (no 0x, no separators)
};

static const BannedAlertChannel BANNED_ALERT_CHANNELS[] = {
  // Public group PSK ("izOH6cXN6mrJ5e26oRXNcg==")
  { "PUBLIC", "8b3387e9c5cdea6ac9e5edbaa115cd72" },
  // sha256("#test")[0..15] — auto-responders in many regions
  { "#test",  "9cd8fcf22a47333b591d96a2b848b73f" },
  // sha256("#bot")[0..15] — generic bot channel, frequent auto-responders
  { "#bot",   "eb50a1bcb3e4e5d7bf69a57c9dada211" },
};

const char* alertReporterBannedChannelMatch(const uint8_t* secret16) {
  char hex[33];
  mesh::Utils::toHex(hex, secret16, 16);
  for (size_t i = 0; i < sizeof(BANNED_ALERT_CHANNELS) / sizeof(BANNED_ALERT_CHANNELS[0]); i++) {
    if (strcmp(hex, BANNED_ALERT_CHANNELS[i].secret_hex) == 0) {
      return BANNED_ALERT_CHANNELS[i].label;
    }
  }
  return nullptr;
}

const char* alertReporterBannedChannelMatchHex(const char* psk_hex) {
  if (!psk_hex || strlen(psk_hex) != 32) return nullptr;
  uint8_t secret[16];
  if (!mesh::Utils::fromHex(secret, 16, psk_hex)) return nullptr;
  return alertReporterBannedChannelMatch(secret);
}

#ifdef WITH_MQTT_BRIDGE
bool AlertReporter::resolveChannel(mesh::GroupChannel& out) const {
  if (!_prefs) return false;

  // alert_psk_hex is the single source of truth — `set alert.hashtag`
  // pre-derives the hex-encoded PSK from sha256("#name")[0..15] at CLI time.
  // Only 16-byte secrets (32 hex chars) are supported; 32-byte channel keys
  // are not used anywhere in MeshCore practice and not represented in the
  // banned table either.
  const char* psk = _obs->alert_psk_hex;
  if (strlen(psk) != 32) return false;

  memset(out.secret, 0, sizeof(out.secret));
  if (!mesh::Utils::fromHex(out.secret, 16, psk)) return false;

  // Belt-and-suspenders against an operator pasting a banned PSK directly
  // into alert.psk, or a hashtag whose hash somehow collides with one of the
  // banned 16-byte secrets (astronomically improbable, but free to check).
  const char* banned = alertReporterBannedChannelMatch(out.secret);
  if (banned) {
    ALERT_DEBUG_PRINTLN("refused banned channel '%s' for alert", banned);
    return false;
  }

  mesh::Utils::sha256(out.hash, sizeof(out.hash), out.secret, 16);
  return true;
}

void AlertReporter::onConfigChanged() {
  // Reset transient state so a config change re-arms the edge detector.
#ifdef WITH_MQTT_BRIDGE
  _wifi.state = OK;
  _wifi.fired_at_ms = 0;
  for (size_t i = 0; i < sizeof(_mqtt) / sizeof(_mqtt[0]); i++) {
    _mqtt[i].state = OK;
    _mqtt[i].fired_at_ms = 0;
  }
#endif
}

bool AlertReporter::sendChannel(const char* text) {
  if (!_mesh || !_prefs) return false;

  mesh::GroupChannel channel;
  if (!resolveChannel(channel)) return false;

  // Build "<sender>: <text>" plaintext payload. Sender = node name (current).
  uint8_t buf[5 + MAX_ALERT_TEXT_LEN + 32];
  uint32_t timestamp = _mesh->getRTCClock()->getCurrentTime();
  memcpy(buf, &timestamp, 4);
  buf[4] = ALERT_TXT_TYPE_PLAIN;

  const char* sender = _prefs->node_name[0] ? _prefs->node_name : "node";
  int n = snprintf((char*)&buf[5], MAX_ALERT_TEXT_LEN, "%s: %s", sender, text);
  if (n < 0) return false;
  if (n >= MAX_ALERT_TEXT_LEN) n = MAX_ALERT_TEXT_LEN - 1;

  mesh::Packet* pkt = _mesh->createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, channel,
                                                 buf, 5 + (size_t)n);
  if (!pkt) {
    ALERT_DEBUG_PRINTLN("createGroupDatagram failed (pool empty?)");
    return false;
  }

  // Ride the repeater's default scope (or `alert.region` override) when the
  // host MyMesh provides one — same path MyMesh uses for adverts and
  // broadcast channel messages. Falls back to plain (unscoped) flood when
  // no callbacks are wired or no scope is configured, matching the
  // pre-scoped behavior on builds without RegionMap.
  //
  // path_hash_size must honor the repeater's configured path.hash.mode (1, 2,
  // or 3-byte hashes); the Mesh.h default of 1 would silently downgrade
  // observers running on 2/3-byte regional meshes.
  const uint8_t path_hash_size = (uint8_t)(_prefs->path_hash_mode + 1);
  TransportKey scope;
  bool have_scope = _callbacks && _callbacks->resolveAlertScope(scope) && !scope.isNull();
  if (have_scope) {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;
    _mesh->sendFlood(pkt, codes, 0, path_hash_size);
  } else {
    _mesh->sendFlood(pkt, 0, path_hash_size);
  }
  ALERT_DEBUG_PRINTLN("sent: %s", text);
  return true;
}

bool AlertReporter::sendText(const char* text) {
  // sendText() is the manual entry point (`alert test` CLI). Deliberately
  // does NOT check alert_enabled so operators can verify the PSK / hashtag
  // setup without enabling automatic fault firing.
  if (!_prefs || !text || !*text) return false;
  return sendChannel(text);
}

void AlertReporter::formatAge(unsigned long age_ms, char* out, size_t out_size) const {
  unsigned long secs = age_ms / 1000UL;
  unsigned long h = secs / 3600UL;
  unsigned long m = (secs % 3600UL) / 60UL;
  if (h > 0) {
    snprintf(out, out_size, "%luh%lum", h, m);
  } else {
    snprintf(out, out_size, "%lum", m);
  }
}

void AlertReporter::onLoop(unsigned long now_ms) {
  if (!_prefs || !_obs || !_obs->alert_enabled) return;
  if (!_mesh) return;

  // Throttle: ~5 s cadence. The thresholds are minutes-scale so this is fine.
  if ((long)(now_ms - _next_check_ms) < 0) return;
  _next_check_ms = now_ms + 5000UL;

#ifdef WITH_MQTT_BRIDGE
  // Clamp to a 60-minute floor regardless of what's in NodePrefs. The CLI
  // already enforces this on set, but a stale prefs file or future field
  // tweak shouldn't be able to drag the floor below 1 hour and let a
  // flapping link spam the mesh.
  //
  // The rate limiter only applies between two real sends: fired_at_ms == 0
  // means "never fired since boot/config change", and treating it as a send
  // at millis()==0 would suppress every first alert until uptime reaches
  // min_interval (observed as a 30-minute alert.mqtt threshold not reporting
  // until 60 minutes after a reboot).
  uint16_t cfg_min = _obs->alert_min_interval_min;
  if (cfg_min < 60) cfg_min = 60;
  unsigned long min_interval_ms = (unsigned long)cfg_min * 60000UL;

  // -------- WiFi fault --------
  if (_obs->alert_wifi_minutes > 0) {
    unsigned long wifi_disc_ms = MQTTBridge::getLastWifiDisconnectTime();
    unsigned long wifi_conn_ms = MQTTBridge::getWifiConnectedAtMillis();
    bool wifi_down = (wifi_disc_ms != 0 && wifi_conn_ms == 0);
    unsigned long down_ms = wifi_down ? (now_ms - wifi_disc_ms) : 0;
    unsigned long thresh_ms = (unsigned long)_obs->alert_wifi_minutes * 60000UL;

    if (_wifi.state == OK) {
      if (wifi_down && down_ms >= thresh_ms &&
          (_wifi.fired_at_ms == 0 || (now_ms - _wifi.fired_at_ms) >= min_interval_ms)) {
        char age[16];
        formatAge(down_ms, age, sizeof(age));
        uint8_t reason = MQTTBridge::getLastWifiDisconnectReason();
        char text[80];
        if (reason != 0) {
          snprintf(text, sizeof(text), "WiFi down %s (reason %u)", age, (unsigned)reason);
        } else {
          snprintf(text, sizeof(text), "WiFi down %s", age);
        }
        if (sendChannel(text)) {
          _wifi.state = FIRING;
          _wifi.fired_at_ms = now_ms;
          _wifi.last_outage_started_ms = wifi_disc_ms;
        }
      }
    } else { // FIRING
      if (!wifi_down) {
        unsigned long total = (wifi_conn_ms != 0 && _wifi.last_outage_started_ms != 0)
            ? (wifi_conn_ms - _wifi.last_outage_started_ms) : 0;
        char age[16];
        formatAge(total, age, sizeof(age));
        char text[80];
        snprintf(text, sizeof(text), "WiFi recovered after %s", age);
        sendChannel(text);
        _wifi.state = OK;
      }
    }
  } else if (_wifi.state == FIRING) {
    _wifi.state = OK; // threshold disabled mid-fault: silently re-arm
  }

  // -------- MQTT slot faults --------
  if (_obs->alert_mqtt_minutes > 0 && _bridge != nullptr) {
    int n = MQTTBridge::getRuntimeSlotCount();
    if (n > (int)(sizeof(_mqtt) / sizeof(_mqtt[0]))) n = (int)(sizeof(_mqtt) / sizeof(_mqtt[0]));
    unsigned long thresh_ms = (unsigned long)_obs->alert_mqtt_minutes * 60000UL;

    for (int i = 0; i < n; i++) {
      Fault& f = _mqtt[i];
      if (!_bridge->isSlotEnabledAndAttempted(i)) {
        if (f.state == FIRING) f.state = OK; // slot disabled mid-fault
        continue;
      }
      unsigned long outage_start = _bridge->getSlotCurrentOutageStartMs(i);
      bool down = (outage_start != 0);
      unsigned long down_ms = down ? (now_ms - outage_start) : 0;

      if (f.state == OK) {
        if (down && down_ms >= thresh_ms &&
            (f.fired_at_ms == 0 || (now_ms - f.fired_at_ms) >= min_interval_ms)) {
          char age[16];
          formatAge(down_ms, age, sizeof(age));
          char text[100];
          snprintf(text, sizeof(text), "MQTT slot %d (%s) down %s",
                   i + 1, _bridge->getSlotPresetName(i), age);
          if (sendChannel(text)) {
            f.state = FIRING;
            f.fired_at_ms = now_ms;
            f.last_outage_started_ms = outage_start;
          }
        }
      } else { // FIRING
        if (!down) {
          unsigned long total = (f.last_outage_started_ms != 0)
              ? (now_ms - f.last_outage_started_ms) : 0;
          char age[16];
          formatAge(total, age, sizeof(age));
          char text[100];
          snprintf(text, sizeof(text), "MQTT slot %d (%s) recovered after %s",
                   i + 1, _bridge->getSlotPresetName(i), age);
          sendChannel(text);
          f.state = OK;
        }
      }
    }
  }
#else
  (void)now_ms;
#endif
}
#endif // WITH_MQTT_BRIDGE (AlertReporter methods, part 2)
