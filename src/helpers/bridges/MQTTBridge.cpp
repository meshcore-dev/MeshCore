#include "MQTTBridge.h"

#ifdef WITH_MQTT_BRIDGE

#ifndef MQTT_HOST
  #error "WITH_MQTT_BRIDGE requires MQTT_HOST to be defined"
#endif

#ifndef WIFI_SSID
  #warning "WITH_MQTT_BRIDGE: WIFI_SSID not defined — WiFi must be started externally before bridge.begin()"
#endif

#include <Arduino.h>

MQTTBridge *MQTTBridge::_instance = nullptr;

MQTTBridge::MQTTBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(prefs, mgr, rtc), _mqttClient(_wifiClient), _lastReconnectAttempt(0) {
  _instance = this;
}

void MQTTBridge::begin() {
#if defined(WIFI_SSID)
  if (WiFi.status() != WL_CONNECTED) {
    WIFI_DEBUG_PRINTLN("Starting WiFi SSID=%s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
#if defined(WIFI_PWD)
    WiFi.begin(WIFI_SSID, WIFI_PWD);
#else
    WiFi.begin(WIFI_SSID);
#endif
  }
#else
  if (WiFi.status() != WL_CONNECTED) {
    WIFI_DEBUG_PRINTLN("WiFi not connected — will retry when WiFi is available");
  }
#endif

  _mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  _mqttClient.setCallback(mqttCallback);

  _initialized = true;
  MQTT_DEBUG_PRINTLN("Initialized, broker=%s:%d topic=%s", MQTT_HOST, MQTT_PORT, MQTT_TOPIC);
}

void MQTTBridge::end() {
  if (_mqttClient.connected()) {
    _mqttClient.disconnect();
  }
  _initialized = false;
  MQTT_DEBUG_PRINTLN("Stopped");
}

bool MQTTBridge::reconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    MQTT_DEBUG_PRINTLN("WiFi not connected, skipping MQTT reconnect");
    return false;
  }

  MQTT_DEBUG_PRINTLN("Connecting to %s:%d...", MQTT_HOST, MQTT_PORT);

  const char *clientId = "meshcore-bridge";
  bool ok;

#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
  ok = _mqttClient.connect(clientId, MQTT_USERNAME, MQTT_PASSWORD);
#elif defined(MQTT_USERNAME)
  ok = _mqttClient.connect(clientId, MQTT_USERNAME, nullptr);
#else
  ok = _mqttClient.connect(clientId);
#endif

  if (ok) {
    _mqttClient.subscribe(MQTT_TOPIC);
    MQTT_DEBUG_PRINTLN("Connected, subscribed to %s", MQTT_TOPIC);
  } else {
    MQTT_DEBUG_PRINTLN("Connect failed, rc=%d", _mqttClient.state());
  }
  return ok;
}

void MQTTBridge::loop() {
  if (!_initialized) return;

  if (!_mqttClient.connected()) {
    unsigned long now = millis();
    // Retry every 5 seconds
    if (now - _lastReconnectAttempt >= 5000) {
      _lastReconnectAttempt = now;
      reconnect();
    }
  } else {
    _mqttClient.loop();
  }
}

void MQTTBridge::sendPacket(mesh::Packet *packet) {
  if (!_initialized || !packet) return;

  if (!_mqttClient.connected()) return;

  if (!_seen_packets.hasSeen(packet)) {
    uint8_t buf[MAX_TRANS_UNIT + 1];
    uint16_t len = packet->writeTo(buf);

    if (len == 0 || len > sizeof(buf)) {
      BRIDGE_DEBUG_PRINTLN("TX invalid packet length %d", len);
      return;
    }

    // Encode as uppercase hex
    static const char *hex_chars = "0123456789ABCDEF";
    for (uint16_t i = 0; i < len; i++) {
      _hexBuf[i * 2]     = hex_chars[(buf[i] >> 4) & 0x0F];
      _hexBuf[i * 2 + 1] = hex_chars[buf[i] & 0x0F];
    }
    _hexBuf[len * 2] = '\0';

    if (_mqttClient.publish(MQTT_TOPIC, _hexBuf)) {
      MQTT_DEBUG_PRINTLN("TX len=%d hex_len=%d", len, len * 2);
    } else {
      MQTT_DEBUG_PRINTLN("TX publish failed len=%d", len);
    }
  }
}

void MQTTBridge::onPacketReceived(mesh::Packet *packet) {
  handleReceivedPacket(packet);
}

void MQTTBridge::mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
  if (_instance) {
    _instance->onMqttMessage(topic, payload, length);
  }
}

void MQTTBridge::onMqttMessage(char *topic, uint8_t *payload, unsigned int length) {
  if (!_initialized) return;

  // Expect even number of hex chars
  if (length == 0 || length % 2 != 0) {
    MQTT_DEBUG_PRINTLN("RX invalid hex length %u", length);
    return;
  }

  uint16_t byte_len = length / 2;
  if (byte_len > MAX_TRANS_UNIT + 1) {
    MQTT_DEBUG_PRINTLN("RX packet too large %u bytes", byte_len);
    return;
  }

  // Decode hex into a temporary buffer
  uint8_t buf[MAX_TRANS_UNIT + 1];
  for (uint16_t i = 0; i < byte_len; i++) {
    char hi = (char)payload[i * 2];
    char lo = (char)payload[i * 2 + 1];

    uint8_t hi_val, lo_val;
    if      (hi >= '0' && hi <= '9') hi_val = hi - '0';
    else if (hi >= 'A' && hi <= 'F') hi_val = hi - 'A' + 10;
    else if (hi >= 'a' && hi <= 'f') hi_val = hi - 'a' + 10;
    else { MQTT_DEBUG_PRINTLN("RX invalid hex char '%c'", hi); return; }

    if      (lo >= '0' && lo <= '9') lo_val = lo - '0';
    else if (lo >= 'A' && lo <= 'F') lo_val = lo - 'A' + 10;
    else if (lo >= 'a' && lo <= 'f') lo_val = lo - 'a' + 10;
    else { MQTT_DEBUG_PRINTLN("RX invalid hex char '%c'", lo); return; }

    buf[i] = (hi_val << 4) | lo_val;
  }

  mesh::Packet *pkt = _mgr->allocNew();
  if (!pkt) {
    MQTT_DEBUG_PRINTLN("RX alloc failed");
    return;
  }

  if (pkt->readFrom(buf, byte_len)) {
    MQTT_DEBUG_PRINTLN("RX len=%u", byte_len);
    onPacketReceived(pkt);
  } else {
    MQTT_DEBUG_PRINTLN("RX parse failed len=%u", byte_len);
    _mgr->free(pkt);
  }
}

#endif
