#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include "CommonCLI.h"

#ifdef WITH_MQTT_BRIDGE
#include "bridges/MQTTBridge.h"
#endif

/**
 * Returns the label of a banned alert channel if \a secret16 matches one of
 * the channels in the BANNED_ALERT_CHANNELS table (e.g. "PUBLIC", "#test",
 * "#bot"), or nullptr otherwise. Centralized here so both AlertReporter and
 * the CommonCLI `set alert.psk` / `set alert.hashtag` handlers can share one
 * source of truth — adding a new banned channel is a one-line table edit.
 */
const char* alertReporterBannedChannelMatch(const uint8_t* secret16);

/**
 * Convenience: hex-decodes \a psk_hex (32 lowercase/uppercase hex chars) and
 * forwards to alertReporterBannedChannelMatch. Returns nullptr if not banned
 * (or if the input isn't a valid 32-char hex string — only 16-byte secrets
 * are present in the banned table).
 */
const char* alertReporterBannedChannelMatchHex(const char* psk_hex);

/**
 * \brief Send-only group-channel "fault alert" reporter for repeater/observer
 *        builds.
 *
 * Polls WiFi and per-MQTT-slot outage timers from MQTTBridge. When any timer
 * exceeds its configured threshold, floods a single PAYLOAD_TYPE_GRP_TXT
 * message on the configured alert channel ("WiFi down 47m — MyObserver"),
 * then arms a "recovered" message for the next state transition.
 *
 * The alert channel must be explicitly configured to either a private hex
 * PSK (`set alert.psk`) or a hashtag name (`set alert.hashtag`); the
 * well-known PUBLIC group key (and a small list of other auto-responder
 * channels — see BANNED_ALERT_CHANNELS in AlertReporter.cpp) are rejected on
 * purpose so fault alerts never spam community channels.
 *
 * Edge-triggered + rate-limited via NodePrefs::alert_min_interval_min so a
 * flapping link cannot spam the channel.
 *
 * Designed to compile and run on any repeater build:
 *  - The channel-send path uses only mesh::Mesh primitives that already
 *    exist in the Dispatcher hierarchy (createGroupDatagram + sendFlood).
 *  - WiFi/MQTT polling is #ifdef WITH_MQTT_BRIDGE-gated; without it, the
 *    reporter still supports manual `alert test` sends.
 */
#ifdef WITH_MQTT_BRIDGE
class AlertReporter {
public:
  AlertReporter();

  /**
   * Wire up the reporter. Must be called from MyMesh::begin() after prefs
   * are loaded. \a callbacks is optional — when non-null the reporter uses
   * it to resolve a TransportKey scope for outgoing alert floods (so the
   * packet rides the repeater's default scope or an `alert.region` override).
   */
  void begin(NodePrefs* prefs, MQTTPrefs* obs, mesh::Mesh* mesh, CommonCLICallbacks* callbacks = nullptr);

#ifdef WITH_MQTT_BRIDGE
  /** Bridge can be (re)created lazily; pass nullptr to detach. */
  void setBridge(MQTTBridge* bridge);
#endif

  /**
   * Re-derive the cached GroupChannel from \a alert_psk_hex. Call from the
   * CLI hot-reload hook after `set alert.psk` / `set alert.hashtag` / `set alert on|off`.
   */
  void onConfigChanged();

  /**
   * Cooperative tick. Fast: returns immediately if disabled, throttled
   * internally to ~5 s checks. Safe to call every loop().
   */
  void onLoop(unsigned long now_ms);

  /**
   * Send an arbitrary text immediately (used by `alert test` CLI). Returns
   * false when disabled, PSK invalid, or the underlying flood-send fails.
   * Bypasses the rate limiter and edge logic.
   */
  bool sendText(const char* text);

private:
  bool resolveChannel(mesh::GroupChannel& out) const;
  bool sendChannel(const char* text);
  void formatAge(unsigned long age_ms, char* out, size_t out_size) const;

  enum FaultState { OK, FIRING };
  struct Fault {
    FaultState state;
    unsigned long fired_at_ms;     // millis() when we last sent a "down" alert
    unsigned long last_outage_started_ms; // remembered so the recovered msg can quote duration
  };

  NodePrefs* _prefs;
  MQTTPrefs* _obs;
  mesh::Mesh* _mesh;
  CommonCLICallbacks* _callbacks;
#ifdef WITH_MQTT_BRIDGE
  MQTTBridge* _bridge;
  Fault _wifi;
  Fault _mqtt[RUNTIME_MQTT_SLOTS];
#endif
  unsigned long _next_check_ms;
};
#endif // WITH_MQTT_BRIDGE
