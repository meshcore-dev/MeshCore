#pragma once

#include <string.h>
#include "MQTTPresets.h"  // MAX_MQTT_SLOTS

// Classification of the config keys the web portal is allowed to drive through
// the CLI `set` handlers. Factored out of WebConfigServer.cpp so the allowlist
// and the (attacker-facing) key parsing can be unit-tested on the host without
// pulling in the whole ESP32 web server (see test/test_webconfig_keys).
//
// Everything here is pure string logic. The functions are `static inline` so
// each translation unit that includes this gets its own copy (there are only
// two: WebConfigServer.cpp and the test), avoiding any ODR concern.

// Keys mapping to CLI `set <key> <value>` handlers. Everything not listed here
// is rejected, so a crafted request can't reach arbitrary commands (`erase`,
// etc.) through the batch. The portal's admin-password field is classified
// separately, see wcIsAdminPasswordKey below.
static const char* const WC_ALLOWED_SET_KEYS[] = {
  // NodePrefs (radio / node)
  "name", "lat", "lon", "radio", "tx", "af", "rxdelay", "txdelay",
  "cad", "radio.rxgain", "repeat", "advert.interval", "flood.advert.interval",
  "flood.max", "flood.max.advert", "flood.max.unscoped", "loop.detect",
  // MQTTPrefs (WiFi / MQTT / misc observer)
  "wifi.ssid", "wifi.pwd", "wifi.powersave",
  "mqtt.origin", "mqtt.iata", "mqtt.status", "mqtt.packets", "mqtt.raw",
  "mqtt.tx", "mqtt.rx", "mqtt.interval", "mqtt.neighbors", "mqtt.neighbors.interval",
  "mqtt.ntp", "mqtt.owner", "mqtt.email",
  "timezone", "timezone.offset", "snmp", "snmp.community",
};
static const char* const WC_ALLOWED_SLOT_KEYS[] = {
  "preset", "server", "port", "username", "password", "token", "topic", "audience",
  "filter",
};

// True when `key` is a well-formed per-slot key ("mqttN.<field>" with N in
// 1..MAX_MQTT_SLOTS). The shortest such key is "mqttN.x" (7 chars), and this
// probes key[4..6], so the length guard must come first — an attacker-supplied
// "mqtt" or "m" would otherwise read past the terminator.
static inline bool wcIsSlotKeyPrefix(const char* key) {
  return strlen(key) >= 7 && memcmp(key, "mqtt", 4) == 0
      && key[4] >= '1' && key[4] <= ('0' + MAX_MQTT_SLOTS) && key[5] == '.';
}

static inline bool wcIsAllowedSetKey(const char* key) {
  for (size_t i = 0; i < sizeof(WC_ALLOWED_SET_KEYS) / sizeof(WC_ALLOWED_SET_KEYS[0]); i++) {
    if (strcmp(key, WC_ALLOWED_SET_KEYS[i]) == 0) return true;
  }
  // mqtt<1-6>.<field>
  if (wcIsSlotKeyPrefix(key)) {
    for (size_t i = 0; i < sizeof(WC_ALLOWED_SLOT_KEYS) / sizeof(WC_ALLOWED_SLOT_KEYS[0]); i++) {
      if (strcmp(&key[6], WC_ALLOWED_SLOT_KEYS[i]) == 0) return true;
    }
  }
  return false;
}

// The admin password maps to the top-level `password` command, not a setter, so
// it is classified apart from the `set` allowlist. It is the only key that gets
// this treatment, which is what keeps the allowlist the sole route to `set` and
// leaves no general path from a batch to arbitrary top-level CLI commands.
static inline bool wcIsAdminPasswordKey(const char* key) {
  return strcmp(key, "password") == 0;
}

static inline bool wcIsValidAdminPassword(const char* value) {
  if (value == NULL) return false;
  const size_t len = strlen(value);
  if (len == 0 || len > 15) return false;  // NodePrefs::password[16], including NUL
  for (size_t i = 0; i < len; i++) {
    if (value[i] == '\r' || value[i] == '\n') return false;  // reject, never silently strip
  }
  return true;
}

// Keys carrying a secret whose stored value is masked with the placeholder in
// the UI; a POST echoing the placeholder for one of these is dropped (unchanged).
static inline bool wcIsSecretKey(const char* key) {
  if (strcmp(key, "wifi.pwd") == 0) return true;
  if (wcIsSlotKeyPrefix(key)
      && (strcmp(&key[6], "password") == 0 || strcmp(&key[6], "token") == 0)) return true;
  return false;
}

// CommonCLI answers a secret getter in plaintext only for the serial console
// (sender_timestamp 0) and masks it for remote callers. The web CLI executes
// with sender_timestamp 0 — that is what makes `erase`, `stats-*` and `set freq`
// reachable — so it would otherwise inherit the serial console's plaintext
// answers for an HTTP request. This says which `get` commands must be masked
// back down, restoring the distinction for a caller not at the serial port.
//
// Writing these has always been possible from the portal; reading them never
// was, because handleConfigGet masks them (wcIsSecretKey). The two are different
// capabilities: replacing a WiFi password does not reveal the current one, and
// replacing an identity does not reveal the existing private key.
static inline bool wcIsSecretReadCommand(const char* cmd) {
  if (strncmp(cmd, "get ", 4) != 0) return false;
  const char* key = cmd + 4;
  while (*key == ' ') key++;
  if (strcmp(key, "prv.key") == 0) return true;         // this node's identity
  if (strcmp(key, "guest.password") == 0) return true;
  if (strcmp(key, "alert.psk") == 0) return true;
  if (strcmp(key, "bridge.secret") == 0) return true;
  return wcIsSecretKey(key);   // wifi.pwd, mqttN.password, mqttN.token
}

// Browser-generated request IDs are exactly eight random bytes encoded as
// hexadecimal. Keeping the grammar deliberately small makes the ID safe to
// echo in JSON/logs and prevents an empty or truncated ID from weakening the
// save/result correlation contract.
static inline bool wcIsValidReqId(const char* reqid) {
  if (reqid == NULL || strlen(reqid) != 16) return false;
  for (size_t i = 0; i < 16; i++) {
    char c = reqid[i];
    if (!((c >= '0' && c <= '9') ||
          (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}
