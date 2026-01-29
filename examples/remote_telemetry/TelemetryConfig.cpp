#include "TelemetryConfig.h"

#include <ArduinoJson.h>
#include <cstring>

namespace {
constexpr size_t CONFIG_JSON_CAPACITY = 4096;
constexpr size_t REPEATER_JSON_CAPACITY = 3072;

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

bool isZeroKey(const std::array<uint8_t, PUB_KEY_SIZE>& key) {
  for (auto byte : key) {
    if (byte != 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

namespace telemetry {

bool RepeaterConfig::isValid() const {
  if (name.length() == 0) {
    return false;
  }
  return !isZeroKey(pubKey);
}

void Settings::applyDefaults() {
  mqttHost = "";
  mqttPort = 1883;
  mqttUsername = "";
  mqttPassword = "";
  mqttTelemetryTopic = "meshcore/it-telemetry/rx";
  mqttStatusTopic = "meshcore/status";
  mqttControlTopic = "meshcore/control";
  pollIntervalMs = 30UL * 60UL * 1000UL;
  loginRetryMs = 120000UL;
  timeoutRetryMs = 30000UL;
  repeaters.clear();
}

bool Settings::isValid() const {
  if (mqttHost.length() == 0) {
    return false;
  }
  if (mqttTelemetryTopic.length() == 0) {
    return false;
  }
  if (repeaters.empty()) {
    return false;
  }
  for (const auto& repeater : repeaters) {
    if (!repeater.isValid()) {
      return false;
    }
  }
  return true;
}

bool decodeHexKey(const char* hex, std::array<uint8_t, PUB_KEY_SIZE>& out) {
  if (!hex) {
    return false;
  }
  size_t len = strlen(hex);
  if (len != PUB_KEY_SIZE * 2) {
    return false;
  }
  for (size_t i = 0; i < PUB_KEY_SIZE; ++i) {
    int hi = hexNibble(hex[i * 2]);
    int lo = hexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

String encodeHexKey(const std::array<uint8_t, PUB_KEY_SIZE>& key) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  String encoded;
  encoded.reserve(PUB_KEY_SIZE * 2);
  for (auto byte : key) {
    encoded += HEX_DIGITS[(byte >> 4) & 0x0F];
    encoded += HEX_DIGITS[byte & 0x0F];
  }
  return encoded;
}

bool parseRepeatersJson(const char* json, std::vector<RepeaterConfig>& out, String& error) {
  if (!json) {
    error = F("Repeater payload missing");
    return false;
  }

  DynamicJsonDocument doc(REPEATER_JSON_CAPACITY);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = err.c_str();
    return false;
  }

  if (!doc.is<JsonArray>()) {
    error = F("Repeaters JSON must be array");
    return false;
  }

  std::vector<RepeaterConfig> parsed;
  parsed.reserve(doc.size());

  for (JsonObject obj : doc.as<JsonArray>()) {
    RepeaterConfig repeater;
    repeater.name = obj["name"].as<const char*>();
    if (obj.containsKey("password")) {
      const char* pass = obj["password"].as<const char*>();
      repeater.password = pass ? pass : "";
    } else {
      repeater.password = "";
    }
    const char* key = obj["pubKey"].as<const char*>();

    if (!decodeHexKey(key, repeater.pubKey)) {
      error = F("Invalid repeater pubKey");
      return false;
    }

    if (!repeater.isValid()) {
      error = F("Incomplete repeater entry");
      return false;
    }

    parsed.push_back(repeater);
  }

  if (parsed.empty()) {
    error = F("At least one repeater required");
    return false;
  }

  out = std::move(parsed);
  return true;
}

String repeatersToJson(const std::vector<RepeaterConfig>& repeaters) {
  DynamicJsonDocument doc(REPEATER_JSON_CAPACITY);
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& repeater : repeaters) {
    JsonObject entry = arr.createNestedObject();
    entry["name"] = repeater.name;
    entry["password"] = repeater.password;
    entry["pubKey"] = encodeHexKey(repeater.pubKey);
  }
  String json;
  serializeJson(arr, json);
  return json;
}

ConfigStore::ConfigStore(fs::FS& fs, const char* path)
  : _fs(fs), _path(path ? path : "/telemetry.json") {
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

  if (JsonObject mqtt = doc["mqtt"].as<JsonObject>()) {
    if (const char* host = mqtt["host"].as<const char*>()) {
      loaded.mqttHost = host;
    }
    if (mqtt.containsKey("port")) {
      loaded.mqttPort = mqtt["port"].as<uint16_t>();
    }
    if (const char* username = mqtt["username"].as<const char*>()) {
      loaded.mqttUsername = username;
    }
    if (const char* password = mqtt["password"].as<const char*>()) {
      loaded.mqttPassword = password;
    }
    if (const char* telemetryTopic = mqtt["telemetryTopic"].as<const char*>()) {
      loaded.mqttTelemetryTopic = telemetryTopic;
    }
    if (const char* statusTopic = mqtt["statusTopic"].as<const char*>()) {
      loaded.mqttStatusTopic = statusTopic;
    }
    if (const char* controlTopic = mqtt["controlTopic"].as<const char*>()) {
      loaded.mqttControlTopic = controlTopic;
    }
  }

  if (JsonObject intervals = doc["intervals"].as<JsonObject>()) {
    if (intervals.containsKey("pollMs")) {
      loaded.pollIntervalMs = intervals["pollMs"].as<unsigned long>();
    }
    if (intervals.containsKey("loginRetryMs")) {
      loaded.loginRetryMs = intervals["loginRetryMs"].as<unsigned long>();
    }
    if (intervals.containsKey("timeoutRetryMs")) {
      loaded.timeoutRetryMs = intervals["timeoutRetryMs"].as<unsigned long>();
    }
  }

  if (JsonArray repeaters = doc["repeaters"].as<JsonArray>()) {
    loaded.repeaters.clear();
    for (JsonObject obj : repeaters) {
      RepeaterConfig repeater;
      repeater.name = obj["name"].as<const char*>();
      if (obj.containsKey("password")) {
        const char* pass = obj["password"].as<const char*>();
        repeater.password = pass ? pass : "";
      } else {
        repeater.password = "";
      }
      const char* key = obj["pubKey"].as<const char*>();
      if (!decodeHexKey(key, repeater.pubKey) || !repeater.isValid()) {
        continue;
      }
      loaded.repeaters.push_back(repeater);
    }
  }

  if (!loaded.repeaters.empty()) {
    _settings = loaded;
  }

  return _settings.isValid();
}

bool ConfigStore::save() const {
  DynamicJsonDocument doc(CONFIG_JSON_CAPACITY);

  JsonObject mqtt = doc.createNestedObject("mqtt");
  mqtt["host"] = _settings.mqttHost;
  mqtt["port"] = _settings.mqttPort;
  mqtt["username"] = _settings.mqttUsername;
  mqtt["password"] = _settings.mqttPassword;
  mqtt["telemetryTopic"] = _settings.mqttTelemetryTopic;
  mqtt["statusTopic"] = _settings.mqttStatusTopic;
  mqtt["controlTopic"] = _settings.mqttControlTopic;

  JsonObject intervals = doc.createNestedObject("intervals");
  intervals["pollMs"] = _settings.pollIntervalMs;
  intervals["loginRetryMs"] = _settings.loginRetryMs;
  intervals["timeoutRetryMs"] = _settings.timeoutRetryMs;

  JsonArray repeaters = doc.createNestedArray("repeaters");
  for (const auto& repeater : _settings.repeaters) {
    JsonObject entry = repeaters.createNestedObject();
    entry["name"] = repeater.name;
    entry["password"] = repeater.password;
    entry["pubKey"] = encodeHexKey(repeater.pubKey);
  }

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

}  // namespace telemetry
