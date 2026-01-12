#include "ChatBotConfig.h"

#include <ArduinoJson.h>
#include <cstring>

namespace {
constexpr size_t CONFIG_JSON_CAPACITY = 2048;
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;

int hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

String toLowerHex(const String& value) {
  String sanitized;
  sanitized.reserve(value.length());
  for (char c : value) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      continue;
    }
    if (hexNibble(c) < 0) {
      return String();
    }
    sanitized += static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  return sanitized;
}

}  // namespace

namespace chatbot {

void Settings::applyDefaults() {
  wifiSsid = "ssid";
  wifiPassword = "password";
  mqttHost = "mqtt.example.com";
  mqttPort = DEFAULT_MQTT_PORT;
  mqttUsername = "";
  mqttPassword = "";
  mqttControlTopic = "meshcore/chatbot/control";
  mqttRxTopic = "meshcore/chatbot/rx";
  mqttTxTopic = "meshcore/chatbot/tx";
  meshNodeName = "";
  meshChannelName = "IT-Telemetry";
  meshChannelKey = "bf0244470ec8b05c6991f0834532b935";
}

bool Settings::isValid() const {
  if (wifiSsid.length() == 0) {
    return false;
  }
  if (mqttHost.length() == 0) {
    return false;
  }
  if (mqttControlTopic.length() == 0 || mqttRxTopic.length() == 0 || mqttTxTopic.length() == 0) {
    return false;
  }
  if (meshChannelName.length() == 0) {
    return false;
  }
  if (!isChannelKeyValid(meshChannelKey)) {
    return false;
  }
  return true;
}

bool decodeHexKey(const char* hex, std::vector<uint8_t>& out) {
  if (!hex) {
    return false;
  }
  size_t len = strlen(hex);
  if (len == 0 || (len % 2) != 0) {
    return false;
  }
  out.clear();
  out.reserve(len / 2);
  for (size_t i = 0; i < len; i += 2) {
    int hi = hexNibble(hex[i]);
    int lo = hexNibble(hex[i + 1]);
    if (hi < 0 || lo < 0) {
      out.clear();
      return false;
    }
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return true;
}

String encodeHexKey(const uint8_t* data, size_t len) {
  static const char HEX_DIGITS_LOWER[] = "0123456789abcdef";
  if (!data || len == 0) {
    return String();
  }
  String encoded;
  encoded.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    encoded += HEX_DIGITS_LOWER[(data[i] >> 4) & 0x0F];
    encoded += HEX_DIGITS_LOWER[data[i] & 0x0F];
  }
  return encoded;
}

String normalizeChannelKey(const String& hex) {
  return toLowerHex(hex);
}

bool isChannelKeyValid(const String& hex) {
  if (hex.length() == 0) {
    return false;
  }
  String sanitized = normalizeChannelKey(hex);
  if (sanitized.length() == 0) {
    return false;
  }
  std::vector<uint8_t> key;
  if (!decodeHexKey(sanitized.c_str(), key)) {
    return false;
  }
  return key.size() == 16 || key.size() == 32;
}

ConfigStore::ConfigStore(CHATBOT_FILESYSTEM& fs, const char* path)
  : _fs(fs), _path(path ? path : "/chatbot.json") {
  _settings.applyDefaults();
}

Settings& ConfigStore::data() {
  return _settings;
}

const Settings& ConfigStore::data() const {
  return _settings;
}

bool ConfigStore::load() {
  File file = _fs.open(_path, FILE_READ);
  if (!file) {
    return false;
  }

  DynamicJsonDocument doc(CONFIG_JSON_CAPACITY);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }

  Settings loaded;
  loaded.applyDefaults();

  if (JsonObject wifi = doc["wifi"].as<JsonObject>()) {
    if (const char* ssid = wifi["ssid"].as<const char*>()) {
      loaded.wifiSsid = ssid;
    }
    if (const char* password = wifi["password"].as<const char*>()) {
      loaded.wifiPassword = password;
    }
  }

  if (JsonObject mqtt = doc["mqtt"].as<JsonObject>()) {
    if (const char* host = mqtt["host"].as<const char*>()) {
      loaded.mqttHost = host;
    }
    if (mqtt.containsKey("port")) {
      uint16_t port = mqtt["port"].as<uint16_t>();
      loaded.mqttPort = port == 0 ? DEFAULT_MQTT_PORT : port;
    }
    if (const char* username = mqtt["username"].as<const char*>()) {
      loaded.mqttUsername = username;
    }
    if (const char* password = mqtt["password"].as<const char*>()) {
      loaded.mqttPassword = password;
    }
    if (const char* control = mqtt["control"].as<const char*>()) {
      loaded.mqttControlTopic = control;
    } else if (const char* controlLegacy = mqtt["controlTopic"].as<const char*>()) {
      loaded.mqttControlTopic = controlLegacy;
    }
    if (const char* rx = mqtt["rx"].as<const char*>()) {
      loaded.mqttRxTopic = rx;
    } else if (const char* rxLegacy = mqtt["rxTopic"].as<const char*>()) {
      loaded.mqttRxTopic = rxLegacy;
    }
    if (const char* tx = mqtt["tx"].as<const char*>()) {
      loaded.mqttTxTopic = tx;
    } else if (const char* txLegacy = mqtt["txTopic"].as<const char*>()) {
      loaded.mqttTxTopic = txLegacy;
    }
  }

  if (JsonObject mesh = doc["mesh"].as<JsonObject>()) {
    if (const char* nodeName = mesh["node"].as<const char*>()) {
      loaded.meshNodeName = nodeName;
    } else if (const char* nodeLegacy = mesh["nodeName"].as<const char*>()) {
      loaded.meshNodeName = nodeLegacy;
    }
    if (const char* channelName = mesh["channel"].as<const char*>()) {
      loaded.meshChannelName = channelName;
    } else if (const char* channelLegacy = mesh["channelName"].as<const char*>()) {
      loaded.meshChannelName = channelLegacy;
    }
    if (const char* key = mesh["key"].as<const char*>()) {
      loaded.meshChannelKey = normalizeChannelKey(String(key));
    } else if (const char* keyLegacy = mesh["channelKey"].as<const char*>()) {
      loaded.meshChannelKey = normalizeChannelKey(String(keyLegacy));
    }
  }

  if (!isChannelKeyValid(loaded.meshChannelKey)) {
    loaded.meshChannelKey = _settings.meshChannelKey;
  }

  _settings = loaded;
  return true;
}

bool ConfigStore::save() const {
  DynamicJsonDocument doc(CONFIG_JSON_CAPACITY);

  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = _settings.wifiSsid;
  wifi["password"] = _settings.wifiPassword;

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = _settings.mqttHost;
  mqtt["port"] = _settings.mqttPort == 0 ? DEFAULT_MQTT_PORT : _settings.mqttPort;
  mqtt["username"] = _settings.mqttUsername;
  mqtt["password"] = _settings.mqttPassword;
  mqtt["control"] = _settings.mqttControlTopic;
  mqtt["rx"] = _settings.mqttRxTopic;
  mqtt["tx"] = _settings.mqttTxTopic;

  JsonObject mesh = doc.createNestedObject("mesh");
  mesh["node"] = _settings.meshNodeName;
  mesh["channel"] = _settings.meshChannelName;
  mesh["key"] = normalizeChannelKey(_settings.meshChannelKey);

  File file = _fs.open(_path, FILE_WRITE);
  if (!file) {
    return false;
  }
  bool ok = serializeJson(doc, file) > 0;
  file.close();
  return ok;
}

void ConfigStore::applyDefaults() {
  _settings.applyDefaults();
}

void ConfigStore::clearSecrets() {
  _settings.wifiPassword = "";
  _settings.mqttPassword = "";
}

}  // namespace chatbot
