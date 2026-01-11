#pragma once

#include <Arduino.h>
#include <FS.h>
#include <Mesh.h>

#include <array>
#include <vector>

namespace telemetry {

struct RepeaterConfig {
  String name;
  String password;
  std::array<uint8_t, PUB_KEY_SIZE> pubKey;

  bool isValid() const;
};

struct Settings {
  String mqttHost;
  uint16_t mqttPort;
  String mqttUsername;
  String mqttPassword;
  String mqttTelemetryTopic;
  String mqttStatusTopic;
  String mqttControlTopic;
  unsigned long pollIntervalMs;
  unsigned long loginRetryMs;
  unsigned long timeoutRetryMs;
  std::vector<RepeaterConfig> repeaters;

  void applyDefaults();
  bool isValid() const;
};

bool decodeHexKey(const char* hex, std::array<uint8_t, PUB_KEY_SIZE>& out);
String encodeHexKey(const std::array<uint8_t, PUB_KEY_SIZE>& key);
bool parseRepeatersJson(const char* json, std::vector<RepeaterConfig>& out, String& error);
String repeatersToJson(const std::vector<RepeaterConfig>& repeaters);

class ConfigStore {
public:
  ConfigStore(fs::FS& fs, const char* path);

  Settings& data();
  const Settings& data() const;

  bool load();
  bool save() const;
  void applyDefaults();

private:
  fs::FS& _fs;
  String _path;
  Settings _settings;
};

}  // namespace telemetry
