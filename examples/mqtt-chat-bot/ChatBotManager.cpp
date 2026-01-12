#include "ChatBotManager.h"

#include "ChatBotMesh.h"

#include <Utils.h>

namespace {
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000UL;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 8000UL;
constexpr size_t MQTT_BUFFER_SIZE = 768;

String payloadToString(const uint8_t* payload, size_t length) {
  String result;
  result.reserve(length + 1);
  for (size_t i = 0; i < length; ++i) {
    result += static_cast<char>(payload[i]);
  }
  return result;
}

String trimCopy(const String& value) {
  String copy = value;
  copy.trim();
  return copy;
}

}  // namespace

ChatBotManager* ChatBotManager::_instance = nullptr;

ChatBotManager::ChatBotManager(ChatBotMesh& mesh, PubSubClient& mqttClient, chatbot::Settings& settings)
  : _mesh(mesh),
    _mqtt(mqttClient),
    _settings(&settings),
    _store(nullptr),
    _nextWifiRetry(0),
    _nextMqttRetry(0),
    _subscriptionsActive(false),
    _wifiStatusLogged(false) {
  _mesh.setManager(this);
  _instance = this;
  _mqtt.setCallback(ChatBotManager::mqttCallback);
  _mqtt.setBufferSize(MQTT_BUFFER_SIZE);
}

void ChatBotManager::attachConfigStore(chatbot::ConfigStore& store) {
  _store = &store;
  _settings = &_store->data();
  refreshClientId();
}

void ChatBotManager::begin() {
  _mesh.begin();
  refreshClientId();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  ensureWifi();
  ensureMqtt();
}

void ChatBotManager::loop() {
  bool wifiReady = ensureWifi();
  bool mqttReady = wifiReady && ensureMqtt();
  if (mqttReady && !_subscriptionsActive) {
    subscribeTopics();
  }
  if (_mqtt.connected()) {
    _mqtt.loop();
  }
}

void ChatBotManager::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  if (_instance) {
    _instance->onMqttMessage(topic, payload, length);
  }
}

void ChatBotManager::onMqttMessage(const char* topic, const uint8_t* payload, size_t length) {
  if (!_settings) {
    return;
  }
  String topicStr(topic ? topic : "");
  String body = payloadToString(payload, length);
  if (topicStr == _settings->mqttControlTopic) {
    handleControlMessage(body);
  } else if (topicStr == _settings->mqttTxTopic) {
    handleTxMessage(body);
  }
}

void ChatBotManager::handleControlMessage(const String& payload) {
  if (!_settings || !_settings->mqttControlTopic.length()) {
    return;
  }

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    publishError("invalid_json", err.c_str());
    return;
  }

  const char* command = doc["command"].as<const char*>();
  const char* correlation = doc["correlationId"].as<const char*>();
  if (!correlation) {
    correlation = doc["correlation_id"].as<const char*>();
  }

  if (!command) {
    return;
  }

  if (strcmp(command, "get_config") == 0) {
    publishConfigSnapshot(correlation);
    return;
  }

  if (strcmp(command, "set_config") == 0) {
    JsonObjectConst data = doc["data"].as<JsonObjectConst>();
    if (data.isNull()) {
      publishError("missing_data", nullptr, correlation);
      return;
    }

    String error;
    if (!applyConfigUpdate(data, error)) {
      publishError("invalid_config", error.length() ? error.c_str() : nullptr, correlation);
      return;
    }

    if (_store && !_store->save()) {
      publishError("persist_failed", nullptr, correlation);
    }
    publishStatus("config_updated", nullptr, correlation);
    publishConfigSnapshot(correlation);
    return;
  }

  publishError("unknown_command", command, correlation);
}

void ChatBotManager::handleTxMessage(const String& payload) {
  if (!_settings || !_settings->mqttTxTopic.length()) {
    return;
  }

  String body = payload;
  body.trim();
  if (body.length() == 0) {
    return;
  }

  String text;
  String senderOverride;
  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (!err && doc.is<JsonObject>()) {
    if (doc.containsKey("text")) {
      text = doc["text"].as<String>();
    }
    if (doc.containsKey("sender")) {
      senderOverride = doc["sender"].as<String>();
    }
  } else {
    text = body;
  }

  text.trim();
  if (text.length() == 0) {
    return;
  }

  String senderName = senderOverride.length() ? trimCopy(senderOverride) : (_settings ? _settings->meshNodeName : String("MeshBot"));
  if (!_mesh.sendChannelMessage(senderName, text)) {
    publishError("mesh_send_failed", "Unable to send mesh message");
  } else {
    publishStatus("tx_forwarded");
  }
}

bool ChatBotManager::ensureWifi() {
  if (!_settings || _settings->wifiSsid.length() == 0) {
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!_wifiStatusLogged) {
      String ip = WiFi.localIP().toString();
      String mac = WiFi.macAddress();
      Serial.printf("[chatbot] WiFi connected, IP %s MAC %s\n", ip.c_str(), mac.c_str());
      _wifiStatusLogged = true;
    }
    return true;
  }

  _wifiStatusLogged = false;

  unsigned long now = millis();
  if (now < _nextWifiRetry) {
    return false;
  }
  _nextWifiRetry = now + WIFI_RETRY_INTERVAL_MS;

  Serial.printf("[chatbot] Connecting WiFi to %s\n", _settings->wifiSsid.c_str());
  if (_settings->wifiPassword.length() > 0) {
    WiFi.begin(_settings->wifiSsid.c_str(), _settings->wifiPassword.c_str());
  } else {
    WiFi.begin(_settings->wifiSsid.c_str());
  }
  return false;
}

bool ChatBotManager::ensureMqtt() {
  if (!_settings || _settings->mqttHost.length() == 0) {
    return false;
  }

  if (_mqtt.connected()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  unsigned long now = millis();
  if (now < _nextMqttRetry) {
    return false;
  }
  _nextMqttRetry = now + MQTT_RETRY_INTERVAL_MS;

  uint16_t port = _settings->mqttPort == 0 ? 1883 : _settings->mqttPort;
  _mqtt.setServer(_settings->mqttHost.c_str(), port);

  if (_clientId.length() == 0) {
    refreshClientId();
  }

  const char* user = _settings->mqttUsername.length() ? _settings->mqttUsername.c_str() : nullptr;
  const char* pass = _settings->mqttPassword.length() ? _settings->mqttPassword.c_str() : nullptr;

  Serial.printf("[chatbot] Connecting MQTT to %s:%u\n", _settings->mqttHost.c_str(), port);
  if (!_mqtt.connect(_clientId.c_str(), user, pass)) {
    Serial.printf("[chatbot] MQTT connect failed (rc=%d)\n", _mqtt.state());
    return false;
  }

  Serial.println(F("[chatbot] MQTT connected"));
  resetSubscriptions();
  subscribeTopics();
  publishStatus("mqtt_connected");
  return true;
}

void ChatBotManager::subscribeTopics() {
  if (!_settings || !_mqtt.connected()) {
    return;
  }
  if (_settings->mqttControlTopic.length()) {
    _mqtt.subscribe(_settings->mqttControlTopic.c_str());
    _lastControlTopic = _settings->mqttControlTopic;
  } else {
    _lastControlTopic.clear();
  }
  if (_settings->mqttTxTopic.length()) {
    _mqtt.subscribe(_settings->mqttTxTopic.c_str());
    _lastTxTopic = _settings->mqttTxTopic;
  } else {
    _lastTxTopic.clear();
  }
  _subscriptionsActive = true;
}

void ChatBotManager::resetSubscriptions() {
  _subscriptionsActive = false;
  _lastControlTopic.clear();
  _lastTxTopic.clear();
}

void ChatBotManager::publishStatus(const char* event, const char* detail, const char* correlationId) {
  if (!_settings || !_mqtt.connected() || !_settings->mqttControlTopic.length()) {
    return;
  }
  StaticJsonDocument<256> doc;
  doc["event"] = event;
  doc["source"] = "chatbot";
  if (detail && detail[0]) {
    doc["detail"] = detail;
  }
  if (correlationId && correlationId[0]) {
    doc["correlationId"] = correlationId;
  }
  String payload;
  serializeJson(doc, payload);
  _mqtt.publish(_settings->mqttControlTopic.c_str(), payload.c_str());
}

void ChatBotManager::publishConfigSnapshot(const char* correlationId) {
  if (!_settings || !_mqtt.connected() || !_settings->mqttControlTopic.length()) {
    return;
  }
  StaticJsonDocument<640> doc;
  doc["event"] = "config";
  doc["source"] = "chatbot";
  if (correlationId && correlationId[0]) {
    doc["correlationId"] = correlationId;
  }

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = _settings->wifiSsid;
  wifi["password"] = _settings->wifiPassword.length() ? "set" : "";

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = _settings->mqttHost;
  mqtt["port"] = _settings->mqttPort == 0 ? 1883 : _settings->mqttPort;
  mqtt["username"] = _settings->mqttUsername;
  mqtt["password"] = _settings->mqttPassword.length() ? "set" : "";
  mqtt["control"] = _settings->mqttControlTopic;
  mqtt["rx"] = _settings->mqttRxTopic;
  mqtt["tx"] = _settings->mqttTxTopic;

  JsonObject mesh = doc.createNestedObject("mesh");
  mesh["node"] = _settings->meshNodeName;
  mesh["channel"] = _settings->meshChannelName;
  mesh["key"] = chatbot::normalizeChannelKey(_settings->meshChannelKey);

  String payload;
  serializeJson(doc, payload);
  _mqtt.publish(_settings->mqttControlTopic.c_str(), payload.c_str());
}

void ChatBotManager::publishError(const char* code, const char* detail, const char* correlationId) {
  if (!_settings || !_mqtt.connected() || !_settings->mqttControlTopic.length()) {
    return;
  }
  StaticJsonDocument<256> doc;
  doc["event"] = "error";
  doc["source"] = "chatbot";
  doc["code"] = code ? code : "unknown";
  if (detail && detail[0]) {
    doc["detail"] = detail;
  }
  if (correlationId && correlationId[0]) {
    doc["correlationId"] = correlationId;
  }
  String payload;
  serializeJson(doc, payload);
  _mqtt.publish(_settings->mqttControlTopic.c_str(), payload.c_str());
}

bool ChatBotManager::applyConfigUpdate(JsonObjectConst data, String& error) {
  if (!_settings) {
    error = F("settings_unavailable");
    return false;
  }

  chatbot::Settings updated = *_settings;
  bool wifiChanged = false;
  bool mqttChanged = false;
  bool meshChanged = false;
  bool topicsChanged = false;

  if (JsonObjectConst wifi = data["wifi"].as<JsonObjectConst>()) {
    if (wifi.containsKey("ssid")) {
      updated.wifiSsid = trimCopy(wifi["ssid"].as<String>());
      wifiChanged = true;
    }
    if (wifi.containsKey("password")) {
      updated.wifiPassword = wifi["password"].as<String>();
      wifiChanged = true;
    }
  }

  if (JsonObjectConst mqtt = data["mqtt"].as<JsonObjectConst>()) {
    if (mqtt.containsKey("host")) {
      String host = trimCopy(mqtt["host"].as<String>());
      if (host.length() == 0) {
        error = F("mqtt_host_required");
        return false;
      }
      updated.mqttHost = host;
      mqttChanged = true;
    }
    if (mqtt.containsKey("port")) {
      int port = mqtt["port"].as<int>();
      if (port <= 0 || port > 65535) {
        error = F("mqtt_port_invalid");
        return false;
      }
      updated.mqttPort = static_cast<uint16_t>(port);
      mqttChanged = true;
    }
    if (mqtt.containsKey("username")) {
      updated.mqttUsername = mqtt["username"].as<String>();
      mqttChanged = true;
    }
    if (mqtt.containsKey("password")) {
      updated.mqttPassword = mqtt["password"].as<String>();
      mqttChanged = true;
    }
    if (mqtt.containsKey("control")) {
      String topic = trimCopy(mqtt["control"].as<String>());
      if (topic.length() == 0) {
        error = F("mqtt_control_required");
        return false;
      }
      updated.mqttControlTopic = topic;
      mqttChanged = true;
      topicsChanged = true;
    }
    if (mqtt.containsKey("rx")) {
      String topic = trimCopy(mqtt["rx"].as<String>());
      if (topic.length() == 0) {
        error = F("mqtt_rx_required");
        return false;
      }
      updated.mqttRxTopic = topic;
      mqttChanged = true;
      topicsChanged = true;
    }
    if (mqtt.containsKey("tx")) {
      String topic = trimCopy(mqtt["tx"].as<String>());
      if (topic.length() == 0) {
        error = F("mqtt_tx_required");
        return false;
      }
      updated.mqttTxTopic = topic;
      mqttChanged = true;
      topicsChanged = true;
    }
  }

  if (JsonObjectConst mesh = data["mesh"].as<JsonObjectConst>()) {
    if (mesh.containsKey("node")) {
      String nodeName = trimCopy(mesh["node"].as<String>());
      if (nodeName.length() == 0) {
        error = F("node_required");
        return false;
      }
      updated.meshNodeName = nodeName;
      meshChanged = true;
    }
    if (mesh.containsKey("channel")) {
      String channel = trimCopy(mesh["channel"].as<String>());
      if (channel.length() == 0) {
        error = F("channel_required");
        return false;
      }
      updated.meshChannelName = channel;
      meshChanged = true;
    }
    if (mesh.containsKey("key")) {
      String key = chatbot::normalizeChannelKey(mesh["key"].as<String>());
      if (!chatbot::isChannelKeyValid(key)) {
        error = F("channel_key_invalid");
        return false;
      }
      updated.meshChannelKey = key;
      meshChanged = true;
    }
  }

  if (!updated.isValid()) {
    error = F("config_invalid");
    return false;
  }

  if (meshChanged) {
    if (!_mesh.configureChannel(updated.meshChannelName, updated.meshChannelKey)) {
      error = F("channel_config_failed");
      return false;
    }
  }

  *_settings = updated;
  if (meshChanged) {
    refreshClientId();
    publishStatus("channel_reconfigured");
  }

  if (wifiChanged) {
    WiFi.disconnect(true, true);
    _nextWifiRetry = 0;
  }

  if (mqttChanged) {
    if (_mqtt.connected()) {
      _mqtt.disconnect();
    }
    _nextMqttRetry = 0;
    resetSubscriptions();
  }

  if (topicsChanged && _mqtt.connected()) {
    subscribeTopics();
  }

  return true;
}

void ChatBotManager::handleMeshChannelMessage(uint32_t timestamp, const char* text, bool direct) {
  if (!_settings || !_mqtt.connected() || !_settings->mqttRxTopic.length()) {
    return;
  }

  String raw = text ? String(text) : String();
  String sender;
  String message = raw;
  int sep = raw.indexOf(':');
  if (sep > 0) {
    sender = raw.substring(0, sep);
    message = raw.substring(sep + 1);
    message.trim();
  }

  StaticJsonDocument<384> doc;
  doc["event"] = "mesh_message";
  doc["source"] = "chatbot";
  doc["channel"] = _settings->meshChannelName;
  doc["timestamp"] = timestamp;
  doc["direct"] = direct;
  if (sender.length()) {
    doc["sender"] = trimCopy(sender);
  }
  doc["text"] = message;
  doc["raw"] = raw;

  String payload;
  serializeJson(doc, payload);
  _mqtt.publish(_settings->mqttRxTopic.c_str(), payload.c_str());
}

void ChatBotManager::notifySendTimeout() {
  publishStatus("mesh_send_timeout");
}

void ChatBotManager::refreshClientId() {
  if (!_settings) {
    _clientId = "meshcore-chatbot";
    return;
  }
  String base = _settings->meshNodeName.length() ? _settings->meshNodeName : String("meshcore-chatbot");
  String suffix = buildHexSuffix(_mesh.self_id.pub_key, 4);
  _clientId = base + "-" + suffix;
}

String ChatBotManager::buildHexSuffix(const uint8_t* data, size_t len) {
  static const char HEX_DIGITS_UPPER[] = "0123456789ABCDEF";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += HEX_DIGITS_UPPER[(data[i] >> 4) & 0x0F];
    out += HEX_DIGITS_UPPER[data[i] & 0x0F];
  }
  return out;
}
