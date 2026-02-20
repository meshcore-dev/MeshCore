#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "MyMesh.h"

#define BAP_CONFIG_FILE "/bap_config.bin"

class BAPConfigManager {
public:
  BAPConfigManager();

  // Load/save configuration from SPIFFS
  bool load(BAPConfig& config);
  bool save(const BAPConfig& config);
  void reset();

  // Serial command handling
  void handleCommand(const char* command, char* reply, BAPConfig& config);
  void printConfig(const BAPConfig& config, Print& output);

  // Individual setters
  void setStopId(BAPConfig& config, uint32_t stop_id);
  void setWiFi(BAPConfig& config, const char* ssid, const char* password);
  void clearWiFi(BAPConfig& config);
  void setRepeater(BAPConfig& config, bool enabled);
  void setRole(BAPConfig& config, uint8_t role);
  void setApiKey(BAPConfig& config, const char* api_key);

  // Get role description
  const char* getRoleString(const BAPConfig& config);

private:
  bool _dirty;  // Config needs saving
  Preferences _prefs;
  static constexpr const char* NVS_NAMESPACE = "bap_secrets";

  // Migrate secrets from old SPIFFS format to NVS
  void migrateToNVS(BAPConfig& config);
};
