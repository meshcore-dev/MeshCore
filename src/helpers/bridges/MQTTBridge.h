#pragma once

#ifdef WITH_MQTT_BRIDGE

#ifndef MQTT_PORT
  #define MQTT_PORT 1883
#endif

#ifndef MQTT_TOPIC
  #define MQTT_TOPIC "meshcore/packets"
#endif

// MQTT_MAX_PACKET_SIZE must fit: MQTT headers + topic + hex payload
// Max hex payload = (MAX_TRANS_UNIT+1)*2 = 370 chars; add topic and MQTT overhead
#ifndef MQTT_MAX_PACKET_SIZE
  #define MQTT_MAX_PACKET_SIZE 512
#endif

#include "helpers/bridges/BridgeBase.h"
#include <WiFi.h>
#include <PubSubClient.h>

#if MQTT_DEBUG && ARDUINO
  #define MQTT_DEBUG_PRINTLN(F, ...) Serial.printf("%s MQTT: " F "\n", getLogDateTime(), ##__VA_ARGS__)
#else
  #define MQTT_DEBUG_PRINTLN(...) {}
#endif

/**
 * @brief Bridge implementation using MQTT over WiFi for packet transport
 *
 * Publishes and subscribes to an MQTT topic, bridging LoRa mesh packets
 * to/from the MQTT broker. Packets are encoded as uppercase ASCII hex strings.
 *
 * Configuration build defines:
 *   WITH_MQTT_BRIDGE   — enable this bridge
 *   MQTT_HOST          — broker hostname or IP (required)
 *   MQTT_PORT          — broker port (default 1883)
 *   MQTT_USERNAME      — broker username (optional, omit to disable auth)
 *   MQTT_PASSWORD      — broker password (optional)
 *   MQTT_TOPIC         — topic to publish/subscribe (default "meshcore/packets")
 *   MQTT_DEBUG         — set to 1 to enable debug output on Serial
 *
 * WiFi must be configured separately via WIFI_SSID / WIFI_PWD build defines,
 * or by calling WiFi.begin() before this bridge's begin() is invoked. The
 * bridge will not attempt an MQTT connection until WiFi is associated.
 *
 * Packet format: raw wire bytes encoded as uppercase hex, e.g.
 *   "1180EF4C22171A42..."
 * Incoming messages on the topic are decoded and injected into the mesh.
 * Duplicate detection via SimpleMeshTables prevents re-forwarding seen packets.
 */
class MQTTBridge : public BridgeBase {
public:
  MQTTBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc, const uint8_t *pubKey);

  char mqtt_host[64];
  uint mqtt_port;
  char mqtt_topic[64];

  void begin() override;
  void end() override;
  void loop() override;
  void sendPacket(mesh::Packet *packet) override;
  void onPacketReceived(mesh::Packet *packet) override;
  void initialize();
  bool reconnect();

private:
  static MQTTBridge *_instance;

  WiFiClient _wifiClient;
  PubSubClient _mqttClient;
  unsigned long _lastReconnectAttempt;
  const uint8_t *_pubKey;

  char _mqtt_username[32];
  char _mqtt_password[64];

  // Buffer sized for maximum hex-encoded mesh packet
  static constexpr size_t HEX_BUF_SIZE = (MAX_TRANS_UNIT + 1) * 2 + 1;
  char _hexBuf[HEX_BUF_SIZE];

  static void mqttCallback(char *topic, uint8_t *payload, unsigned int length);
  void onMqttMessage(char *topic, uint8_t *payload, unsigned int length);
};

#endif
