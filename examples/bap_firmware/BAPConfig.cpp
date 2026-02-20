#include "BAPConfig.h"
#include <SPIFFS.h>

BAPConfigManager::BAPConfigManager() : _dirty(false) {
}

bool BAPConfigManager::load(BAPConfig& config) {
  // Set defaults first
  memset(&config, 0, sizeof(config));
  config.node_role = BAP_ROLE_AUTO;
  config.stop_id = 0;
  config.is_repeater = 0;

  File f = SPIFFS.open(BAP_CONFIG_FILE, "r");
  if (!f) {
    return false;  // No config file, use defaults
  }

  size_t bytes_read = f.read((uint8_t*)&config, sizeof(config));
  f.close();

  if (bytes_read != sizeof(config)) {
    return false;
  }

  // Check if we need to migrate secrets from SPIFFS to NVS
  // Old format had secrets stored directly in the struct
  // New format: secrets stored in NVS, struct fields cleared
  if (config.wifi_password[0] != '\0' || config.api_key[0] != '\0') {
    migrateToNVS(config);
  }

  // Load secrets from NVS
  _prefs.begin(NVS_NAMESPACE, true);  // read-only mode
  if (_prefs.isKey("wifi_pass")) {
    _prefs.getString("wifi_pass", config.wifi_password, sizeof(config.wifi_password));
  }
  if (_prefs.isKey("api_key")) {
    _prefs.getString("api_key", config.api_key, sizeof(config.api_key));
  }
  _prefs.end();

  return true;
}

bool BAPConfigManager::save(const BAPConfig& config) {
  // Save non-sensitive config to SPIFFS
  // Create a copy without secrets for SPIFFS storage
  BAPConfig config_copy = config;
  memset(config_copy.wifi_password, 0, sizeof(config_copy.wifi_password));
  memset(config_copy.api_key, 0, sizeof(config_copy.api_key));

  File f = SPIFFS.open(BAP_CONFIG_FILE, "w");
  if (!f) {
    return false;
  }

  size_t bytes_written = f.write((const uint8_t*)&config_copy, sizeof(config_copy));
  f.close();

  // Save secrets to NVS
  _prefs.begin(NVS_NAMESPACE, false);  // read-write mode
  if (config.wifi_password[0] != '\0') {
    _prefs.putString("wifi_pass", config.wifi_password);
  }
  if (config.api_key[0] != '\0') {
    _prefs.putString("api_key", config.api_key);
  }
  _prefs.end();

  _dirty = false;
  return bytes_written == sizeof(config_copy);
}

void BAPConfigManager::migrateToNVS(BAPConfig& config) {
  // Migrate secrets from old SPIFFS format to NVS
  _prefs.begin(NVS_NAMESPACE, false);

  if (config.wifi_password[0] != '\0') {
    _prefs.putString("wifi_pass", config.wifi_password);
    memset(config.wifi_password, 0, sizeof(config.wifi_password));
  }

  if (config.api_key[0] != '\0') {
    _prefs.putString("api_key", config.api_key);
    memset(config.api_key, 0, sizeof(config.api_key));
  }

  _prefs.end();

  // Re-save the config without secrets to SPIFFS
  BAPConfig config_copy = config;
  File f = SPIFFS.open(BAP_CONFIG_FILE, "w");
  if (f) {
    f.write((const uint8_t*)&config_copy, sizeof(config_copy));
    f.close();
  }
}

void BAPConfigManager::reset() {
  SPIFFS.remove(BAP_CONFIG_FILE);

  // Clear NVS secrets
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.clear();
  _prefs.end();

  _dirty = false;
}

void BAPConfigManager::handleCommand(const char* command, char* reply, BAPConfig& config) {
  reply[0] = '\0';

  // Parse command
  if (strncmp(command, "setstop ", 8) == 0) {
    uint32_t stop_id = atol(command + 8);
    // Input validation: stop_id should be 4-7 digits
    if (stop_id < 1000 || stop_id > 9999999) {
      strcpy(reply, "Invalid stop ID (must be 4-7 digits)");
    } else {
      setStopId(config, stop_id);
      save(config);
      sprintf(reply, "Stop ID set to %u", stop_id);
    }
  }
  else if (strncmp(command, "setwifi ", 8) == 0) {
    // Parse "setwifi ssid password"
    const char* ssid = command + 8;
    const char* space = strchr(ssid, ' ');
    if (space) {
      int ssid_len = space - ssid;
      if (ssid_len < 32) {
        char ssid_buf[32];
        strncpy(ssid_buf, ssid, ssid_len);
        ssid_buf[ssid_len] = '\0';
        setWiFi(config, ssid_buf, space + 1);
        save(config);
        sprintf(reply, "WiFi set to '%s'", ssid_buf);
      } else {
        strcpy(reply, "SSID too long (max 31 chars)");
      }
    } else {
      strcpy(reply, "Usage: setwifi <ssid> <password>");
    }
  }
  else if (strcmp(command, "clearwifi") == 0) {
    clearWiFi(config);
    save(config);
    strcpy(reply, "WiFi cleared - switching to display mode");
  }
  else if (strncmp(command, "setrepeater ", 12) == 0) {
    int enabled = atoi(command + 12);
    setRepeater(config, enabled != 0);
    save(config);
    sprintf(reply, "Repeater mode %s", enabled ? "enabled" : "disabled");
  }
  else if (strncmp(command, "setapikey ", 10) == 0) {
    setApiKey(config, command + 10);
    save(config);
    sprintf(reply, "API key set");
  }
  else if (strcmp(command, "showconfig") == 0) {
    snprintf(reply, 160,
      "Role: %s | Stop: %u | Repeater: %s | WiFi: %s | API: %s",
      getRoleString(config),
      config.stop_id,
      config.is_repeater ? "ON" : "OFF",
      config.wifi_ssid[0] ? config.wifi_ssid : "(not set)",
      config.api_key[0] ? "configured" : "(not set)"
    );
  }
  else if (strcmp(command, "reset") == 0) {
    reset();
    // Reset config to defaults
    memset(&config, 0, sizeof(config));
    config.node_role = BAP_ROLE_AUTO;
    config.stop_id = 0;
    config.is_repeater = 0;
    strcpy(reply, "Configuration reset to defaults");
  }
  else if (strcmp(command, "help") == 0) {
    strcpy(reply,
      "Commands: setstop <id>, setwifi <ssid> <pass>, setapikey <key>, "
      "clearwifi, setrepeater <0|1>, showconfig, reset"
    );
  }
}

void BAPConfigManager::printConfig(const BAPConfig& config, Print& output) {
  output.println("=== BAP Configuration ===");
  output.printf("Role:       %s\n", getRoleString(config));
  output.printf("Stop ID:    %u\n", config.stop_id);
  output.printf("Repeater:   %s\n", config.is_repeater ? "Enabled" : "Disabled");
  output.printf("WiFi SSID:  %s\n", config.wifi_ssid[0] ? config.wifi_ssid : "(not set)");
  output.printf("API Key:    %s\n", config.api_key[0] ? "configured" : "(not set)");
}

void BAPConfigManager::setStopId(BAPConfig& config, uint32_t stop_id) {
  config.stop_id = stop_id;
  _dirty = true;
}

void BAPConfigManager::setWiFi(BAPConfig& config, const char* ssid, const char* password) {
  strncpy(config.wifi_ssid, ssid, sizeof(config.wifi_ssid) - 1);
  config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = '\0';
  strncpy(config.wifi_password, password, sizeof(config.wifi_password) - 1);
  config.wifi_password[sizeof(config.wifi_password) - 1] = '\0';
  _dirty = true;
}

void BAPConfigManager::clearWiFi(BAPConfig& config) {
  memset(config.wifi_ssid, 0, sizeof(config.wifi_ssid));
  memset(config.wifi_password, 0, sizeof(config.wifi_password));

  // Also clear from NVS
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.remove("wifi_pass");
  _prefs.end();

  _dirty = true;
}

void BAPConfigManager::setRepeater(BAPConfig& config, bool enabled) {
  config.is_repeater = enabled ? 1 : 0;
  _dirty = true;
}

void BAPConfigManager::setApiKey(BAPConfig& config, const char* api_key) {
  strncpy(config.api_key, api_key, sizeof(config.api_key) - 1);
  config.api_key[sizeof(config.api_key) - 1] = '\0';

  // Save to NVS immediately for security
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString("api_key", api_key);
  _prefs.end();

  _dirty = true;
}

void BAPConfigManager::setRole(BAPConfig& config, uint8_t role) {
  config.node_role = role;
  _dirty = true;
}

const char* BAPConfigManager::getRoleString(const BAPConfig& config) {
  // Determine effective role
  if (config.node_role == BAP_ROLE_AUTO) {
    return config.wifi_ssid[0] ? "Gateway (auto)" : "Display (auto)";
  }
  switch (config.node_role) {
    case BAP_ROLE_GATEWAY: return "Gateway";
    case BAP_ROLE_DISPLAY: return "Display";
    default: return "Unknown";
  }
}
