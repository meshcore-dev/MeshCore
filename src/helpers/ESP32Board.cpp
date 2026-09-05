#ifdef ESP_PLATFORM

#include "ESP32Board.h"
#include <target.h>

#if defined(ADMIN_PASSWORD) && !defined(DISABLE_WIFI_OTA)   // Repeater or Room Server only
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

#include <SPIFFS.h>

bool ESP32Board::startOTAUpdate(const char* id, char reply[], bool force_ap) {
  inhibit_sleep = true;   // prevent sleep during OTA

  // If the device is already on a WiFi network (e.g. an observer joined in STA
  // mode), serve ElegantOTA on the station IP so it's reachable from the LAN
  // without joining a separate AP. Otherwise raise the MeshCore-OTA SoftAP.
  // force_ap ("start ota ap") always raises the SoftAP, so the OTA UI stays
  // reachable even when the joined network applies client isolation and the
  // station IP can't be reached.
  IPAddress ip;
  if (!force_ap && WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP();
  } else {
    WiFi.softAP("MeshCore-OTA", NULL);
    ip = WiFi.softAPIP();
  }

  sprintf(reply, "Started: http://%s/update", ip.toString().c_str());
  MESH_DEBUG_PRINTLN("startOTAUpdate: %s", reply);

  static char id_buf[60];
  sprintf(id_buf, "%s (%s)", id, getManufacturerName());
  static char home_buf[90];
  sprintf(home_buf, "<H2>Hi! I am a MeshCore Repeater. ID: %s</H2>", id);

  AsyncWebServer* server = new AsyncWebServer(80);

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", home_buf);
  });
  server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/packet_log", "text/plain");
  });

  AsyncElegantOTA.setID(id_buf);
  AsyncElegantOTA.begin(server);    // Start ElegantOTA
  server->begin();

  return true;
}

#else
bool ESP32Board::startOTAUpdate(const char* id, char reply[], bool force_ap) {
  return false; // not supported
}
#endif

// ---------------------------------------------------------------------------
// Manifest-driven pull OTA (observer / MQTT-bridge builds only)
//
// The observer already holds a live WiFi station connection (for the MQTT
// bridge) and embeds a root-CA bundle, so it can fetch its own firmware. The
// caller (CommonCLI) stops the MQTT bridge first to free heap/TLS, then calls
// this. We read the web-flasher manifest (config.json), find the `flash-update`
// (app-only) build for our own variant, refuse partition-change releases (OTA
// can't rewrite the partition table), skip if already up to date, then stream
// the .bin straight into the inactive OTA slot via HTTPUpdate.
// ---------------------------------------------------------------------------
#if defined(WITH_MQTT_BRIDGE)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_partition.h>

// Embedded CA bundle (produced by board_build.embed_files). Weak so non-bundle
// builds still link; we check for presence at runtime.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_src_certs_x509_crt_bundle_bin_start") __attribute__((weak));
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_src_certs_x509_crt_bundle_bin_end") __attribute__((weak));

// Extract the trailing build hash. For a filename we first drop a ".bin"
// suffix, then take the token after the last '-'. Works for both the manifest
// asset name ("...-v1.16.0-8b084d5.bin" -> "8b084d5") and the embedded
// FIRMWARE_VERSION ("v1.16.0-observer-8b084d5" -> "8b084d5").
static void ota_extractHash(const char* s, char* out, size_t out_sz) {
  if (!s) { if (out_sz) out[0] = 0; return; }
  size_t len = strlen(s);
  if (len > 4 && strcmp(s + len - 4, ".bin") == 0) len -= 4;
  size_t i = len;
  while (i > 0 && s[i - 1] != '-') i--;
  size_t n = len - i;
  if (n >= out_sz) n = out_sz - 1;
  memcpy(out, s + i, n);
  out[n] = 0;
}

// Split a version token "vMAJOR.MINOR.PATCH[.BUILD]" into its base
// ("vMAJOR.MINOR.PATCH") and build number (BUILD, or -1 if there's no 4th
// component). The base has exactly two dots; a third dot introduces the build.
static void ota_parseVersion(const char* ver, char* base_out, size_t base_sz, int* build_out) {
  *build_out = -1;
  if (base_sz) base_out[0] = 0;
  if (!ver) return;
  int dots = 0, third_dot = -1;
  for (int j = 0; ver[j]; j++) {
    if (ver[j] == '.' && ++dots == 3) { third_dot = j; break; }
  }
  if (third_dot >= 0) {
    size_t n = (size_t)third_dot;
    if (n >= base_sz) n = base_sz - 1;
    memcpy(base_out, ver, n);
    base_out[n] = 0;
    *build_out = atoi(ver + third_dot + 1);
  } else {
    strncpy(base_out, ver, base_sz - 1);
    base_out[base_sz - 1] = 0;
  }
}

// Canonical signature of the FLASHED partition table — MUST match
// scripts/partition_signature.py: each entry "type:subtype:offset:size" in
// lowercase hex, sorted by offset, joined by ','. Lets `ota update` compare the
// target build's partition layout (carried in the manifest as partSig) against
// what's actually on this device, instead of a blanket per-variant flag.
static void ota_partitionSignature(char* out, size_t out_sz) {
  struct PE { uint8_t type, subtype; uint32_t off, size; } e[24];
  int n = 0;
  esp_partition_iterator_t it =
      esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it != nullptr && n < (int)(sizeof(e) / sizeof(e[0]))) {
    const esp_partition_t* p = esp_partition_get(it);
    e[n].type = (uint8_t)p->type;
    e[n].subtype = (uint8_t)p->subtype;
    e[n].off = p->address;
    e[n].size = p->size;
    n++;
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);  // safe on NULL (loop exhausted)
  // insertion sort by offset (matches the script's sort key)
  for (int i = 1; i < n; i++) {
    PE k = e[i];
    int j = i - 1;
    while (j >= 0 && e[j].off > k.off) { e[j + 1] = e[j]; j--; }
    e[j + 1] = k;
  }
  size_t pos = 0;
  if (out_sz) out[0] = 0;
  for (int i = 0; i < n && pos + 1 < out_sz; i++) {
    pos += snprintf(out + pos, out_sz - pos, "%s%x:%x:%x:%x",
                    i ? "," : "", e[i].type, e[i].subtype, (unsigned)e[i].off, (unsigned)e[i].size);
  }
}

// Parameters handed to the worker task; lives on otaFromManifest()'s stack,
// which stays valid because that function blocks until the worker signals done.
struct OtaTaskArgs {
  ESP32Board* self;
  const char* current_ver;
  bool dry_run;
  char* reply;
  volatile bool result;
  volatile bool done;
};

static void ota_task_entry(void* param) {
  OtaTaskArgs* a = static_cast<OtaTaskArgs*>(param);
  a->result = a->self->otaFromManifestImpl(a->current_ver, a->dry_run, a->reply);
  a->done = true;        // on a successful `ota update` we reboot before reaching here
  vTaskDelete(nullptr);
}

bool ESP32Board::otaFromManifest(const char* current_ver, bool dry_run, char reply[]) {
  // The TLS handshake (cert-bundle verify) + JSON parse / HTTPUpdate use far more
  // stack than the ~8 KB loop task offers — especially when reached via the deep
  // mesh-receive call chain (it overflows the loopTask canary). Run the work in a
  // dedicated 24 KB-stack task and block here until it finishes. The big stack is
  // freed when the task exits; on a successful update the chip reboots inside it.
  OtaTaskArgs args = { this, current_ver, dry_run, reply, false, false };
  TaskHandle_t handle = nullptr;
  BaseType_t ok = xTaskCreatePinnedToCore(ota_task_entry, "ota", 24576, &args, 5, &handle, 1);
  if (ok != pdPASS) {
    strcpy(reply, "ERR: OTA task spawn failed");
    return false;
  }
  while (!args.done) {
    delay(50);  // Arduino delay() yields to other tasks
  }
  return args.result;
}

bool ESP32Board::otaFromManifestImpl(const char* current_ver, bool dry_run, char reply[]) {
#if !defined(OTA_MANIFEST_BASE) || !defined(OTA_VARIANT)
  strcpy(reply, "ERR: OTA not configured (build via build.sh)");
  return false;
#else
  if (WiFi.status() != WL_CONNECTED) {
    strcpy(reply, "ERR: WiFi not connected");
    return false;
  }

  size_t bundle_len = 0;
  if (rootca_crt_bundle_start != nullptr && rootca_crt_bundle_end != nullptr &&
      rootca_crt_bundle_end > rootca_crt_bundle_start) {
    bundle_len = (size_t)(rootca_crt_bundle_end - rootca_crt_bundle_start);
  }
  if (!dry_run && bundle_len == 0) {
    strcpy(reply, "ERR: no embedded cert bundle");
    return false;
  }

  // --- Fetch this variant's slim manifest ----------------------------------
  // <OTA_MANIFEST_BASE>/<OTA_VARIANT>.json — a ~180 byte per-variant file, not the
  // full config.json.
  char murl[200];
  HTTPClient http;
  WiFiClientSecure mclient;  // only used for the HTTPS (update) path

  if (dry_run) {
    // `ota check`: fetch over PLAIN HTTP. With no TLS handshake the fetch costs
    // negligible heap, so the check runs with the MQTT bridge UP even on no-PSRAM
    // — where the cert-bundle TLS verify would otherwise exhaust internal heap
    // alongside the two live MQTT TLS sessions (free heap collapses to a few KB
    // and the handshake + the bridge both fail). This only reads version info; the
    // firmware download below (ota update) is always TLS-verified. Requires the
    // manifest host to serve /v over HTTP (no forced HTTPS redirect).
    if (strncmp(OTA_MANIFEST_BASE, "https://", 8) == 0) {
      snprintf(murl, sizeof(murl), "http://%s/%s.json", OTA_MANIFEST_BASE + 8, OTA_VARIANT);
    } else {
      snprintf(murl, sizeof(murl), "%s/%s.json", OTA_MANIFEST_BASE, OTA_VARIANT);
    }
    if (!http.begin(murl)) {
      strcpy(reply, "ERR: manifest connect failed");
      return false;
    }
  } else {
    // `ota update`: HTTPS. The bridge is torn down for an update so heap is free,
    // and integrity matters because we're about to flash.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    mclient.setCACertBundle(rootca_crt_bundle_start, bundle_len);
#else
    mclient.setCACertBundle(rootca_crt_bundle_start);
#endif
    mclient.setTimeout(15000);
    snprintf(murl, sizeof(murl), "%s/%s.json", OTA_MANIFEST_BASE, OTA_VARIANT);
    if (!http.begin(mclient, murl)) {
      strcpy(reply, "ERR: manifest connect failed");
      return false;
    }
  }

  if (!dry_run) { Serial.print("OTA: checking manifest "); Serial.println(murl); }

  // Force HTTP/1.0: a CDN (e.g. Cloudflare) answers HTTP/1.1 with chunked encoding
  // and no Content-Length; the raw chunked stream corrupts the parse. HTTP/1.0
  // yields a Connection: close, unframed body.
  http.useHTTP10(true);
  http.setTimeout(20000);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(reply, 160, "ERR: manifest HTTP %d", code);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  stream->setTimeout(20000);  // readBytes honours this, so a slow TLS link != EOF

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, *stream);
  http.end();
  if (err) {
    snprintf(reply, 160, "ERR: manifest parse (%s)", err.c_str());
    return false;
  }

  // Copy fields out before the document is reused/cleared.
  char file_url[200] = {0}, avail_version[40] = {0}, avail_base[40] = {0}, avail_hash[24] = {0};
  strncpy(file_url, doc["file"] | "", sizeof(file_url) - 1);
  strncpy(avail_version, doc["version"] | "", sizeof(avail_version) - 1);
  strncpy(avail_base, doc["baseVersion"] | "", sizeof(avail_base) - 1);
  strncpy(avail_hash, doc["hash"] | "", sizeof(avail_hash) - 1);
  int avail_build = doc["build"] | -1;
  bool legacy_partition_change = doc["partitionChange"] | false;
  char manifest_partsig[256] = {0};
  strncpy(manifest_partsig, doc["partSig"] | "", sizeof(manifest_partsig) - 1);
  doc.clear();

  if (!file_url[0]) {
    strcpy(reply, "ERR: manifest missing file");
    return false;
  }

  // Partition compatibility: prefer the precise per-build signature — compare the
  // target build's partition layout (manifest partSig) to what's actually flashed
  // on THIS device. Refuse only on a real mismatch (OTA can't rewrite the table).
  // Fall back to the legacy bool for manifests that predate partSig.
  bool partition_change;
  if (manifest_partsig[0]) {
    char dev_partsig[256];
    ota_partitionSignature(dev_partsig, sizeof(dev_partsig));
    partition_change = (strcmp(dev_partsig, manifest_partsig) != 0);
  } else {
    partition_change = legacy_partition_change;
  }

  // --- Determine current-vs-available --------------------------------------
  // Our running version token (e.g. "v1.16.0.5"), i.e. current_ver up to the
  // first '-' (which precedes "-observer-<hash>").
  char own_version[40] = {0};
  for (size_t i = 0; current_ver && current_ver[i] && current_ver[i] != '-' && i < sizeof(own_version) - 1; i++) {
    own_version[i] = current_ver[i];
  }
  char own_base[40];
  int own_build;
  ota_parseVersion(own_version, own_base, sizeof(own_base), &own_build);

  // Fallback identity by commit hash (handles pre-build-number / local images that
  // carry no 4th version component). Shared-prefix compare absorbs the 7- vs 8-char
  // git abbreviation difference.
  char cur_hash[24];
  ota_extractHash(current_ver, cur_hash, sizeof(cur_hash));
  size_t lh = strlen(avail_hash), lc = strlen(cur_hash);
  size_t m = (lh < lc) ? lh : lc;
  bool hash_equal = (m >= 7 && strncmp(avail_hash, cur_hash, m) == 0);

  bool same_base = (own_base[0] && avail_base[0] && strcmp(own_base, avail_base) == 0);
  bool have_builds = (own_build >= 0 && avail_build >= 0);
  bool diff_base = (own_base[0] && avail_base[0] && !same_base);

  int behind = 0;
  bool up_to_date;
  if (same_base && have_builds) {
    behind = avail_build - own_build;
    up_to_date = (behind <= 0);
  } else if (diff_base) {
    up_to_date = false;  // different base version is always an update
  } else {
    up_to_date = hash_equal;  // unknown build numbers -> fall back to hash
  }

  // Display strings carry the short commit hash in the same form as the asset
  // filename, e.g. "v1.16.0.2 (5acfdd7)" (or just the hash if there's no version).
  char avail_disp[72], own_disp[72];
  if (avail_version[0]) snprintf(avail_disp, sizeof(avail_disp), "%s (%s)", avail_version, avail_hash);
  else                  snprintf(avail_disp, sizeof(avail_disp), "%s", avail_hash);
  if (own_version[0])   snprintf(own_disp, sizeof(own_disp), "%s (%s)", own_version, cur_hash);
  else                  snprintf(own_disp, sizeof(own_disp), "%s", cur_hash);
  const char* pc_note = partition_change ? " [partition change: cable flash]" : "";

  // --- Report (dry run / `ota check`) --------------------------------------
  // Returns true iff an OTA-applicable update is available (not up-to-date and not
  // a partition-change build). `ota check` ignores the return and just shows the
  // reply; `ota update` uses it to decide whether to actually schedule the flash.
  if (dry_run) {
    if (up_to_date) {
      snprintf(reply, 160, "up to date: %s", avail_disp);
    } else if (same_base && have_builds) {
      snprintf(reply, 160, "update available: %s -> %s (%d behind)%s", own_disp, avail_disp, behind, pc_note);
    } else if (diff_base) {
      snprintf(reply, 160, "update available: %s -> %s (new base)%s", own_disp, avail_disp, pc_note);
    } else {
      snprintf(reply, 160, "update available: %s -> %s%s", own_disp, avail_disp, pc_note);
    }
    return (!up_to_date && !partition_change);
  }

  // --- Gates (real `ota update`) -------------------------------------------
  if (partition_change) {
    snprintf(reply, 160, "ERR: %s needs cable flash (partition change)", avail_disp);
    return false;
  }
  if (up_to_date) {
    snprintf(reply, 160, "OK: already up to date: %s", avail_disp);
    return false;
  }

  // --- Stream the .bin (the manifest's full URL) into the inactive OTA slot -
  Serial.printf("OTA: update %s -> %s\n", own_disp, avail_disp);
  Serial.print("OTA: downloading "); Serial.println(file_url);
  inhibit_sleep = true;  // keep awake through the flash

  WiFiClientSecure uclient;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  uclient.setCACertBundle(rootca_crt_bundle_start, bundle_len);
#else
  uclient.setCACertBundle(rootca_crt_bundle_start);
#endif
  uclient.setTimeout(20000);

  // Console progress to the USB serial (always on; MESH_DEBUG is off on the default
  // observer profile). Non-capturing lambdas + a file-static decile, so the global
  // httpUpdate object never holds a dangling reference after this function returns.
  static int ota_progress_decile;
  ota_progress_decile = -1;
  httpUpdate.onProgress([](int cur, int total) {
    if (total <= 0) return;
    int d = (int)((int64_t)cur * 10 / total);
    if (d != ota_progress_decile) { ota_progress_decile = d; Serial.printf("OTA: %d%%\n", d * 10); }
  });
  httpUpdate.onEnd([]() { Serial.println("OTA: write complete, rebooting..."); });
  httpUpdate.rebootOnUpdate(true);  // reboots into the new image on success
  t_httpUpdate_return ret = httpUpdate.update(uclient, file_url);

  // Only reached on failure (success reboots inside update()).
  inhibit_sleep = false;
  snprintf(reply, 160, "ERR: OTA failed (%d): %s", (int)ret,
           httpUpdate.getLastErrorString().c_str());
  Serial.print("OTA: FAILED - "); Serial.println(reply);
  return false;
#endif  // OTA_MANIFEST_BASE && OTA_VARIANT
}
#else
bool ESP32Board::otaFromManifest(const char* current_ver, bool dry_run, char reply[]) {
  strcpy(reply, "ERR: not supported");
  return false;
}
#endif  // WITH_MQTT_BRIDGE

void ESP32Board::powerOff() {
  enterDeepSleep(0); // Do not wakeup
}

void ESP32Board::enterDeepSleep(uint32_t secs) {
  // Power off the display if any
#ifdef DISPLAY_CLASS
  display.turnOff();
#endif

  // Power off LoRa
  radio_driver.powerOff();

  // Keep LoRa inactive during deepsleep
  digitalWrite(P_LORA_NSS, HIGH);
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
  gpio_hold_en((gpio_num_t)P_LORA_NSS);
#else
  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
#endif

  // Power off GPS if any
  if (sensors.getLocationProvider() != NULL) {
    sensors.getLocationProvider()->stop();
  }

  // Flush serial buffers
  Serial.flush();
  delay(100);

  // Clear stale wakeup sources to avoid ghost wakeup
  // This is required when Power Management and automatic lightsleep are enabled
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  if (secs > 0) {
    esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
  }

  // Finally set ESP32 into deepsleep
  esp_deep_sleep_start(); // CPU halts here and never returns!
}
#endif
