#pragma once

#include <Arduino.h>
#include <vector>

#if defined(ESP32) || defined(RP2040_PLATFORM)
  #include <FS.h>
  #define CHATBOT_FILESYSTEM fs::FS
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <Adafruit_LittleFS.h>
  #define CHATBOT_FILESYSTEM Adafruit_LittleFS
  using namespace Adafruit_LittleFS_Namespace;
#else
  #include <FS.h>
  #define CHATBOT_FILESYSTEM fs::FS
#endif

namespace chatbot {

struct Settings {
  String wifiSsid;
  String wifiPassword;
  String mqttHost;
  uint16_t mqttPort;
  String mqttUsername;
  String mqttPassword;
  String mqttControlTopic;
  String mqttRxTopic;
  String mqttTxTopic;
  String meshNodeName;
  String meshChannelName;
  String meshChannelKey;

  void applyDefaults();
  bool isValid() const;
};

bool decodeHexKey(const char* hex, std::vector<uint8_t>& out);
String encodeHexKey(const uint8_t* data, size_t len);
String normalizeChannelKey(const String& hex);
bool isChannelKeyValid(const String& hex);

class ConfigStore {
public:
  ConfigStore(CHATBOT_FILESYSTEM& fs, const char* path);

  Settings& data();
  const Settings& data() const;

  bool load();
  bool save() const;
  void applyDefaults();
  void clearSecrets();

private:
  CHATBOT_FILESYSTEM& _fs;
  String _path;
  Settings _settings;
};

}  // namespace chatbot
