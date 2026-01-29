#include <Arduino.h>
#include "CommonCLI.h"
#include "TxtDataHelpers.h"
#include "AdvertDataHelpers.h"
#include <RTClib.h>
#ifdef ESP_PLATFORM
#include <WiFi.h>
#include <esp_wifi.h>
#endif
#ifdef WITH_MQTT_BRIDGE
#include "bridges/MQTTBridge.h"

// Helper function to calculate total size of MQTT fields for file format compatibility
// Uses NodePrefs struct to get accurate field sizes
static size_t getMQTTFieldsSize(const NodePrefs* prefs) {
  return sizeof(prefs->mqtt_origin) + sizeof(prefs->mqtt_iata) +
         sizeof(prefs->mqtt_status_enabled) + sizeof(prefs->mqtt_packets_enabled) +
         sizeof(prefs->mqtt_raw_enabled) + sizeof(prefs->mqtt_tx_enabled) +
         sizeof(prefs->mqtt_status_interval) + sizeof(prefs->wifi_ssid) +
         sizeof(prefs->wifi_password) + sizeof(prefs->timezone_string) +
         sizeof(prefs->timezone_offset) + sizeof(prefs->mqtt_server) +
         sizeof(prefs->mqtt_port) + sizeof(prefs->mqtt_username) +
         sizeof(prefs->mqtt_password) + sizeof(prefs->mqtt_analyzer_us_enabled) +
         sizeof(prefs->mqtt_analyzer_eu_enabled) + sizeof(prefs->mqtt_owner_public_key) +
         sizeof(prefs->mqtt_email);
}
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

static bool isValidName(const char *n) {
  while (*n) {
    if (*n == '[' || *n == ']' || *n == '/' || *n == '\\' || *n == ':' || *n == ',' || *n == '?' || *n == '*') return false;
    n++;
  }
  return true;
}

void CommonCLI::loadPrefs(FILESYSTEM* fs) {
  bool is_fresh_install = false;
  bool is_upgrade = false;
  
  if (fs->exists("/com_prefs")) {
    loadPrefsInt(fs, "/com_prefs");   // new filename
  } else if (fs->exists("/node_prefs")) {
    loadPrefsInt(fs, "/node_prefs");
    is_upgrade = true;  // Migrating from old filename
    savePrefs(fs);  // save to new filename
    fs->remove("/node_prefs");  // remove old
  } else {
    // File doesn't exist - set default bridge settings for fresh installs
    is_fresh_install = true;
    _prefs->bridge_pkt_src = 1;  // Default to RX (logRx) for new installs
  }
#ifdef WITH_MQTT_BRIDGE
  // Load MQTT preferences from separate file
  loadMQTTPrefs(fs);
  // Sync MQTT prefs to NodePrefs so existing code (like MQTTBridge) can access them
  syncMQTTPrefsToNodePrefs();
  
  // For MQTT bridge, migrate bridge.source to RX (logRx) only on fresh installs or upgrades
  // This ensures new users get the correct default, but respects existing user choices
  // MQTT bridge with TX requires mqtt.tx to be enabled (disabled by default),
  // so RX is the sensible default for MQTT bridge installations
  if ((is_fresh_install || is_upgrade) && _prefs->bridge_pkt_src == 0) {
    MESH_DEBUG_PRINTLN("MQTT Bridge: Migrating bridge.source from tx to rx (MQTT bridge default)");
    _prefs->bridge_pkt_src = 1;  // Set to RX (logRx)
    savePrefs(fs);  // Save the updated preference
  }
#endif
}

void CommonCLI::loadPrefsInt(FILESYSTEM* fs, const char* filename) {
#if defined(RP2040_PLATFORM)
  File file = fs->open(filename, "r");
#else
  File file = fs->open(filename);
#endif
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs->airtime_factor, sizeof(_prefs->airtime_factor));    // 0
    file.read((uint8_t *)&_prefs->node_name, sizeof(_prefs->node_name));              // 4
    file.read(pad, 4);                                                                // 36
    file.read((uint8_t *)&_prefs->node_lat, sizeof(_prefs->node_lat));                // 40
    file.read((uint8_t *)&_prefs->node_lon, sizeof(_prefs->node_lon));                // 48
    file.read((uint8_t *)&_prefs->password[0], sizeof(_prefs->password));             // 56
    file.read((uint8_t *)&_prefs->freq, sizeof(_prefs->freq));                        // 72
    file.read((uint8_t *)&_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));        // 76
    file.read((uint8_t *)&_prefs->disable_fwd, sizeof(_prefs->disable_fwd));          // 77
    file.read((uint8_t *)&_prefs->advert_interval, sizeof(_prefs->advert_interval));  // 78
    file.read((uint8_t *)pad, 1);                                                     // 79  was 'unused'
    file.read((uint8_t *)&_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));      // 80
    file.read((uint8_t *)&_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.read((uint8_t *)&_prefs->guest_password[0], sizeof(_prefs->guest_password)); // 88
    file.read((uint8_t *)&_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor)); // 104
    file.read(pad, 4);                                                                             // 108
    file.read((uint8_t *)&_prefs->sf, sizeof(_prefs->sf));                                         // 112
    file.read((uint8_t *)&_prefs->cr, sizeof(_prefs->cr));                                         // 113
    file.read((uint8_t *)&_prefs->allow_read_only, sizeof(_prefs->allow_read_only));               // 114
    file.read((uint8_t *)&_prefs->multi_acks, sizeof(_prefs->multi_acks));                         // 115
    file.read((uint8_t *)&_prefs->bw, sizeof(_prefs->bw));                                         // 116
    file.read((uint8_t *)&_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));         // 120
    file.read(pad, 3);                                                                             // 121
    file.read((uint8_t *)&_prefs->flood_max, sizeof(_prefs->flood_max));                           // 124
    file.read((uint8_t *)&_prefs->flood_advert_interval, sizeof(_prefs->flood_advert_interval));   // 125
    file.read((uint8_t *)&_prefs->interference_threshold, sizeof(_prefs->interference_threshold)); // 126
    file.read((uint8_t *)&_prefs->bridge_enabled, sizeof(_prefs->bridge_enabled));                 // 127
    file.read((uint8_t *)&_prefs->bridge_delay, sizeof(_prefs->bridge_delay));                     // 128
    file.read((uint8_t *)&_prefs->bridge_pkt_src, sizeof(_prefs->bridge_pkt_src));                 // 130
    file.read((uint8_t *)&_prefs->bridge_baud, sizeof(_prefs->bridge_baud));                       // 131
    file.read((uint8_t *)&_prefs->bridge_channel, sizeof(_prefs->bridge_channel));                 // 135
    file.read((uint8_t *)&_prefs->bridge_secret, sizeof(_prefs->bridge_secret));                   // 136
    file.read((uint8_t *)&_prefs->powersaving_enabled, sizeof(_prefs->powersaving_enabled));       // 152
    file.read(pad, 3);                                                                             // 153
    file.read((uint8_t *)&_prefs->gps_enabled, sizeof(_prefs->gps_enabled));                       // 156
    file.read((uint8_t *)&_prefs->gps_interval, sizeof(_prefs->gps_interval));                     // 157
    file.read((uint8_t *)&_prefs->advert_loc_policy, sizeof (_prefs->advert_loc_policy));          // 161
    file.read((uint8_t *)&_prefs->discovery_mod_timestamp, sizeof(_prefs->discovery_mod_timestamp)); // 162
    file.read((uint8_t *)&_prefs->adc_multiplier, sizeof(_prefs->adc_multiplier)); // 166
    file.read((uint8_t *)_prefs->owner_info, sizeof(_prefs->owner_info));  // 170
    // MQTT settings - skip reading from main prefs file (now stored separately)
    // For backward compatibility, we'll skip these bytes if they exist in old files
    // The actual MQTT prefs will be loaded from /mqtt_prefs in loadMQTTPrefs()
    // Skip MQTT fields for file format compatibility (whether MQTT bridge is enabled or not)
#ifdef WITH_MQTT_BRIDGE
    size_t mqtt_fields_size = getMQTTFieldsSize(_prefs);
#else
    // If MQTT bridge not enabled, still skip these fields for file format compatibility
    size_t mqtt_fields_size = 
      sizeof(_prefs->mqtt_origin) + sizeof(_prefs->mqtt_iata) +
      sizeof(_prefs->mqtt_status_enabled) + sizeof(_prefs->mqtt_packets_enabled) +
      sizeof(_prefs->mqtt_raw_enabled) + sizeof(_prefs->mqtt_tx_enabled) +
      sizeof(_prefs->mqtt_status_interval) + sizeof(_prefs->wifi_ssid) +
      sizeof(_prefs->wifi_password) + sizeof(_prefs->timezone_string) +
      sizeof(_prefs->timezone_offset) + sizeof(_prefs->mqtt_server) +
      sizeof(_prefs->mqtt_port) + sizeof(_prefs->mqtt_username) +
      sizeof(_prefs->mqtt_password) + sizeof(_prefs->mqtt_analyzer_us_enabled) +
      sizeof(_prefs->mqtt_analyzer_eu_enabled) + sizeof(_prefs->mqtt_owner_public_key) +
      sizeof(_prefs->mqtt_email);
#endif
    uint8_t skip_buffer[512]; // Large enough buffer
    size_t remaining = mqtt_fields_size;
    while (remaining > 0) {
      size_t to_read = remaining > sizeof(skip_buffer) ? sizeof(skip_buffer) : remaining;
      file.read(skip_buffer, to_read);
      remaining -= to_read;
    }

    // sanitise bad pref values
    _prefs->rx_delay_base = constrain(_prefs->rx_delay_base, 0, 20.0f);
    _prefs->tx_delay_factor = constrain(_prefs->tx_delay_factor, 0, 2.0f);
    _prefs->direct_tx_delay_factor = constrain(_prefs->direct_tx_delay_factor, 0, 2.0f);
    _prefs->airtime_factor = constrain(_prefs->airtime_factor, 0, 9.0f);
    _prefs->freq = constrain(_prefs->freq, 400.0f, 2500.0f);
    _prefs->bw = constrain(_prefs->bw, 7.8f, 500.0f);
    _prefs->sf = constrain(_prefs->sf, 5, 12);
    _prefs->cr = constrain(_prefs->cr, 5, 8);
    _prefs->tx_power_dbm = constrain(_prefs->tx_power_dbm, 1, 30);
    _prefs->multi_acks = constrain(_prefs->multi_acks, 0, 1);

    // sanitise bad bridge pref values
    _prefs->bridge_enabled = constrain(_prefs->bridge_enabled, 0, 1);
    _prefs->bridge_delay = constrain(_prefs->bridge_delay, 0, 10000);
    _prefs->bridge_pkt_src = constrain(_prefs->bridge_pkt_src, 0, 1);
    _prefs->bridge_baud = constrain(_prefs->bridge_baud, 9600, 115200);
    _prefs->bridge_channel = constrain(_prefs->bridge_channel, 0, 14);

    _prefs->powersaving_enabled = constrain(_prefs->powersaving_enabled, 0, 1);

    _prefs->gps_enabled = constrain(_prefs->gps_enabled, 0, 1);
    _prefs->advert_loc_policy = constrain(_prefs->advert_loc_policy, 0, 2);

    file.close();
  }
}

void CommonCLI::savePrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove("/com_prefs");
  File file = fs->open("/com_prefs", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open("/com_prefs", "w");
#else
  File file = fs->open("/com_prefs", "w", true);
#endif
  if (file) {
    uint8_t pad[8];
    memset(pad, 0, sizeof(pad));

    file.write((uint8_t *)&_prefs->airtime_factor, sizeof(_prefs->airtime_factor));    // 0
    file.write((uint8_t *)&_prefs->node_name, sizeof(_prefs->node_name));              // 4
    file.write(pad, 4);                                                                // 36
    file.write((uint8_t *)&_prefs->node_lat, sizeof(_prefs->node_lat));                // 40
    file.write((uint8_t *)&_prefs->node_lon, sizeof(_prefs->node_lon));                // 48
    file.write((uint8_t *)&_prefs->password[0], sizeof(_prefs->password));             // 56
    file.write((uint8_t *)&_prefs->freq, sizeof(_prefs->freq));                        // 72
    file.write((uint8_t *)&_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));        // 76
    file.write((uint8_t *)&_prefs->disable_fwd, sizeof(_prefs->disable_fwd));          // 77
    file.write((uint8_t *)&_prefs->advert_interval, sizeof(_prefs->advert_interval));  // 78
    file.write((uint8_t *)pad, 1);                                                     // 79  was 'unused'
    file.write((uint8_t *)&_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));      // 80
    file.write((uint8_t *)&_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.write((uint8_t *)&_prefs->guest_password[0], sizeof(_prefs->guest_password)); // 88
    file.write((uint8_t *)&_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor)); // 104
    file.write(pad, 4);                                                                             // 108
    file.write((uint8_t *)&_prefs->sf, sizeof(_prefs->sf));                                         // 112
    file.write((uint8_t *)&_prefs->cr, sizeof(_prefs->cr));                                         // 113
    file.write((uint8_t *)&_prefs->allow_read_only, sizeof(_prefs->allow_read_only));               // 114
    file.write((uint8_t *)&_prefs->multi_acks, sizeof(_prefs->multi_acks));                         // 115
    file.write((uint8_t *)&_prefs->bw, sizeof(_prefs->bw));                                         // 116
    file.write((uint8_t *)&_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));         // 120
    file.write(pad, 3);                                                                             // 121
    file.write((uint8_t *)&_prefs->flood_max, sizeof(_prefs->flood_max));                           // 124
    file.write((uint8_t *)&_prefs->flood_advert_interval, sizeof(_prefs->flood_advert_interval));   // 125
    file.write((uint8_t *)&_prefs->interference_threshold, sizeof(_prefs->interference_threshold)); // 126
    file.write((uint8_t *)&_prefs->bridge_enabled, sizeof(_prefs->bridge_enabled));                 // 127
    file.write((uint8_t *)&_prefs->bridge_delay, sizeof(_prefs->bridge_delay));                     // 128
    file.write((uint8_t *)&_prefs->bridge_pkt_src, sizeof(_prefs->bridge_pkt_src));                 // 130
    file.write((uint8_t *)&_prefs->bridge_baud, sizeof(_prefs->bridge_baud));                       // 131
    file.write((uint8_t *)&_prefs->bridge_channel, sizeof(_prefs->bridge_channel));                 // 135
    file.write((uint8_t *)&_prefs->bridge_secret, sizeof(_prefs->bridge_secret));                   // 136
    file.write((uint8_t *)&_prefs->powersaving_enabled, sizeof(_prefs->powersaving_enabled));       // 152
    file.write(pad, 3);                                                                             // 153
    file.write((uint8_t *)&_prefs->gps_enabled, sizeof(_prefs->gps_enabled));                       // 156
    file.write((uint8_t *)&_prefs->gps_interval, sizeof(_prefs->gps_interval));                     // 157
    file.write((uint8_t *)&_prefs->advert_loc_policy, sizeof(_prefs->advert_loc_policy));           // 161
    file.write((uint8_t *)&_prefs->discovery_mod_timestamp, sizeof(_prefs->discovery_mod_timestamp)); // 162
    file.write((uint8_t *)&_prefs->adc_multiplier, sizeof(_prefs->adc_multiplier));                 // 166
    file.write((uint8_t *)_prefs->owner_info, sizeof(_prefs->owner_info));  // 170
    // MQTT settings - no longer saved here (stored in separate /mqtt_prefs file)
    // Write zeros/padding to maintain file format compatibility
#ifdef WITH_MQTT_BRIDGE
    size_t mqtt_fields_size = getMQTTFieldsSize(_prefs);
#else
    // If MQTT bridge not enabled, still write zeros for file format compatibility
    size_t mqtt_fields_size = 
      sizeof(_prefs->mqtt_origin) + sizeof(_prefs->mqtt_iata) +
      sizeof(_prefs->mqtt_status_enabled) + sizeof(_prefs->mqtt_packets_enabled) +
      sizeof(_prefs->mqtt_raw_enabled) + sizeof(_prefs->mqtt_tx_enabled) +
      sizeof(_prefs->mqtt_status_interval) + sizeof(_prefs->wifi_ssid) +
      sizeof(_prefs->wifi_password) + sizeof(_prefs->timezone_string) +
      sizeof(_prefs->timezone_offset) + sizeof(_prefs->mqtt_server) +
      sizeof(_prefs->mqtt_port) + sizeof(_prefs->mqtt_username) +
      sizeof(_prefs->mqtt_password) + sizeof(_prefs->mqtt_analyzer_us_enabled) +
      sizeof(_prefs->mqtt_analyzer_eu_enabled) + sizeof(_prefs->mqtt_owner_public_key) +
      sizeof(_prefs->mqtt_email);
#endif
    memset(pad, 0, sizeof(pad));
    size_t remaining = mqtt_fields_size;
    while (remaining > 0) {
      size_t to_write = remaining > sizeof(pad) ? sizeof(pad) : remaining;
      file.write(pad, to_write);
      remaining -= to_write;
    }

    file.close();
  }
#ifdef WITH_MQTT_BRIDGE
  // Save MQTT preferences to separate file
  syncNodePrefsToMQTTPrefs();  // Sync any changes from NodePrefs to MQTTPrefs
  saveMQTTPrefs(fs);
#endif
}

#ifdef WITH_MQTT_BRIDGE
// Set default values for MQTT preferences (used when file doesn't exist or is corrupted)
static void setMQTTPrefsDefaults(MQTTPrefs* prefs) {
  memset(prefs, 0, sizeof(MQTTPrefs));
  // Set sensible defaults matching MQTTBridge expectations
  prefs->mqtt_status_enabled = 1;    // enabled by default
  prefs->mqtt_packets_enabled = 1;   // enabled by default
  prefs->mqtt_raw_enabled = 0;       // disabled by default
  prefs->mqtt_tx_enabled = 0;        // disabled by default (RX only)
  prefs->mqtt_status_interval = 300000; // 5 minutes default
  prefs->mqtt_analyzer_us_enabled = 1; // enabled by default
  prefs->mqtt_analyzer_eu_enabled = 1; // enabled by default
  #ifdef MQTT_WIFI_POWER_SAVE_DEFAULT
  prefs->wifi_power_save = MQTT_WIFI_POWER_SAVE_DEFAULT; // 0=min, 1=none, 2=max
  #else
  prefs->wifi_power_save = 0; // Default to WIFI_PS_MIN_MODEM (0=min)
  #endif
  // String fields are already zero-initialized by memset
}

void CommonCLI::loadMQTTPrefs(FILESYSTEM* fs) {
  // Initialize with defaults first
  setMQTTPrefsDefaults(&_mqtt_prefs);
  
  bool file_existed = fs->exists("/mqtt_prefs");
  if (file_existed) {
    // Load from separate MQTT prefs file
#if defined(RP2040_PLATFORM)
    File file = fs->open("/mqtt_prefs", "r");
#else
    File file = fs->open("/mqtt_prefs");
#endif
    if (file) {
      // Verify file size is correct before reading
      if (file.size() >= sizeof(_mqtt_prefs)) {
        size_t bytes_read = file.read((uint8_t *)&_mqtt_prefs, sizeof(_mqtt_prefs));
        if (bytes_read != sizeof(_mqtt_prefs)) {
          // File read incomplete - reinitialize to defaults
          setMQTTPrefsDefaults(&_mqtt_prefs);
        }
      } else {
        // File too small - reinitialize to defaults
        setMQTTPrefsDefaults(&_mqtt_prefs);
      }
      file.close();
    }
  } else {
    // Migration: Try to read from old /com_prefs file if it exists
    // This handles the case where MQTT settings were previously stored in /com_prefs
    if (fs->exists("/com_prefs")) {
#if defined(RP2040_PLATFORM)
      File file = fs->open("/com_prefs", "r");
#else
      File file = fs->open("/com_prefs");
#endif
      if (file) {
        // Skip to MQTT section (after advert_loc_policy at offset 161)
        // Calculate offset: we need to skip everything up to and including advert_loc_policy
        size_t offset_to_mqtt = 
          sizeof(_prefs->airtime_factor) + sizeof(_prefs->node_name) + 4 + // pad
          sizeof(_prefs->node_lat) + sizeof(_prefs->node_lon) +
          sizeof(_prefs->password) + sizeof(_prefs->freq) +
          sizeof(_prefs->tx_power_dbm) + sizeof(_prefs->disable_fwd) +
          sizeof(_prefs->advert_interval) + 1 + // pad
          sizeof(_prefs->rx_delay_base) + sizeof(_prefs->tx_delay_factor) +
          sizeof(_prefs->guest_password) + sizeof(_prefs->direct_tx_delay_factor) + 4 + // pad
          sizeof(_prefs->sf) + sizeof(_prefs->cr) +
          sizeof(_prefs->allow_read_only) + sizeof(_prefs->multi_acks) +
          sizeof(_prefs->bw) + sizeof(_prefs->agc_reset_interval) + 3 + // pad
          sizeof(_prefs->flood_max) + sizeof(_prefs->flood_advert_interval) +
          sizeof(_prefs->interference_threshold) + sizeof(_prefs->bridge_enabled) +
          sizeof(_prefs->bridge_delay) + sizeof(_prefs->bridge_pkt_src) +
          sizeof(_prefs->bridge_baud) + sizeof(_prefs->bridge_channel) +
          sizeof(_prefs->bridge_secret) + 4 + // pad
          sizeof(_prefs->gps_enabled) + sizeof(_prefs->gps_interval) +
          sizeof(_prefs->advert_loc_policy);
        
        // Check if file is large enough and seek succeeded
        if (file.size() >= offset_to_mqtt + sizeof(_mqtt_prefs)) {
          if (file.seek(offset_to_mqtt)) {
            size_t bytes_read = file.read((uint8_t *)&_mqtt_prefs, sizeof(_mqtt_prefs));
            if (bytes_read == sizeof(_mqtt_prefs)) {
              // Successfully migrated - save to new location for future use
              file.close();
              saveMQTTPrefs(fs);
              return; // Migration successful
            }
          }
        }
        file.close();
        // Migration failed - defaults already set, just return
        return;
      }
    }
    // No file exists and migration didn't happen - defaults already set
  }
}

void CommonCLI::saveMQTTPrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove("/mqtt_prefs");
  File file = fs->open("/mqtt_prefs", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open("/mqtt_prefs", "w");
#else
  File file = fs->open("/mqtt_prefs", "w", true);
#endif
  if (file) {
    file.write((uint8_t *)&_mqtt_prefs, sizeof(_mqtt_prefs));
    file.close();
  }
}

void CommonCLI::syncMQTTPrefsToNodePrefs() {
  // Copy MQTT prefs to NodePrefs so existing code can access them
  // Use StrHelper::strncpy to ensure proper null termination
  StrHelper::strncpy(_prefs->mqtt_origin, _mqtt_prefs.mqtt_origin, sizeof(_prefs->mqtt_origin));
  StrHelper::strncpy(_prefs->mqtt_iata, _mqtt_prefs.mqtt_iata, sizeof(_prefs->mqtt_iata));
  _prefs->mqtt_status_enabled = _mqtt_prefs.mqtt_status_enabled;
  _prefs->mqtt_packets_enabled = _mqtt_prefs.mqtt_packets_enabled;
  _prefs->mqtt_raw_enabled = _mqtt_prefs.mqtt_raw_enabled;
  _prefs->mqtt_tx_enabled = _mqtt_prefs.mqtt_tx_enabled;
  _prefs->mqtt_status_interval = _mqtt_prefs.mqtt_status_interval;
  StrHelper::strncpy(_prefs->wifi_ssid, _mqtt_prefs.wifi_ssid, sizeof(_prefs->wifi_ssid));
  StrHelper::strncpy(_prefs->wifi_password, _mqtt_prefs.wifi_password, sizeof(_prefs->wifi_password));
  _prefs->wifi_power_save = _mqtt_prefs.wifi_power_save;
  StrHelper::strncpy(_prefs->timezone_string, _mqtt_prefs.timezone_string, sizeof(_prefs->timezone_string));
  _prefs->timezone_offset = _mqtt_prefs.timezone_offset;
  StrHelper::strncpy(_prefs->mqtt_server, _mqtt_prefs.mqtt_server, sizeof(_prefs->mqtt_server));
  _prefs->mqtt_port = _mqtt_prefs.mqtt_port;
  StrHelper::strncpy(_prefs->mqtt_username, _mqtt_prefs.mqtt_username, sizeof(_prefs->mqtt_username));
  StrHelper::strncpy(_prefs->mqtt_password, _mqtt_prefs.mqtt_password, sizeof(_prefs->mqtt_password));
  _prefs->mqtt_analyzer_us_enabled = _mqtt_prefs.mqtt_analyzer_us_enabled;
  _prefs->mqtt_analyzer_eu_enabled = _mqtt_prefs.mqtt_analyzer_eu_enabled;
  StrHelper::strncpy(_prefs->mqtt_owner_public_key, _mqtt_prefs.mqtt_owner_public_key, sizeof(_prefs->mqtt_owner_public_key));
  StrHelper::strncpy(_prefs->mqtt_email, _mqtt_prefs.mqtt_email, sizeof(_prefs->mqtt_email));
}

void CommonCLI::syncNodePrefsToMQTTPrefs() {
  // Copy NodePrefs to MQTT prefs (used when saving after changes via CLI)
  // Use StrHelper::strncpy to ensure proper null termination
  StrHelper::strncpy(_mqtt_prefs.mqtt_origin, _prefs->mqtt_origin, sizeof(_mqtt_prefs.mqtt_origin));
  StrHelper::strncpy(_mqtt_prefs.mqtt_iata, _prefs->mqtt_iata, sizeof(_mqtt_prefs.mqtt_iata));
  _mqtt_prefs.mqtt_status_enabled = _prefs->mqtt_status_enabled;
  _mqtt_prefs.mqtt_packets_enabled = _prefs->mqtt_packets_enabled;
  _mqtt_prefs.mqtt_raw_enabled = _prefs->mqtt_raw_enabled;
  _mqtt_prefs.mqtt_tx_enabled = _prefs->mqtt_tx_enabled;
  _mqtt_prefs.mqtt_status_interval = _prefs->mqtt_status_interval;
  StrHelper::strncpy(_mqtt_prefs.wifi_ssid, _prefs->wifi_ssid, sizeof(_mqtt_prefs.wifi_ssid));
  StrHelper::strncpy(_mqtt_prefs.wifi_password, _prefs->wifi_password, sizeof(_mqtt_prefs.wifi_password));
  _mqtt_prefs.wifi_power_save = _prefs->wifi_power_save;
  StrHelper::strncpy(_mqtt_prefs.timezone_string, _prefs->timezone_string, sizeof(_mqtt_prefs.timezone_string));
  _mqtt_prefs.timezone_offset = _prefs->timezone_offset;
  StrHelper::strncpy(_mqtt_prefs.mqtt_server, _prefs->mqtt_server, sizeof(_mqtt_prefs.mqtt_server));
  _mqtt_prefs.mqtt_port = _prefs->mqtt_port;
  StrHelper::strncpy(_mqtt_prefs.mqtt_username, _prefs->mqtt_username, sizeof(_mqtt_prefs.mqtt_username));
  StrHelper::strncpy(_mqtt_prefs.mqtt_password, _prefs->mqtt_password, sizeof(_mqtt_prefs.mqtt_password));
  _mqtt_prefs.mqtt_analyzer_us_enabled = _prefs->mqtt_analyzer_us_enabled;
  _mqtt_prefs.mqtt_analyzer_eu_enabled = _prefs->mqtt_analyzer_eu_enabled;
  StrHelper::strncpy(_mqtt_prefs.mqtt_owner_public_key, _prefs->mqtt_owner_public_key, sizeof(_mqtt_prefs.mqtt_owner_public_key));
  StrHelper::strncpy(_mqtt_prefs.mqtt_email, _prefs->mqtt_email, sizeof(_mqtt_prefs.mqtt_email));
}
#endif

#define MIN_LOCAL_ADVERT_INTERVAL   60

void CommonCLI::savePrefs() {
  uint8_t old_advert_interval = _prefs->advert_interval;
  if (_prefs->advert_interval * 2 < MIN_LOCAL_ADVERT_INTERVAL) {
    _prefs->advert_interval = 0;  // turn it off, now that device has been manually configured
  }
  // If advert_interval was changed, update the timer to reflect the change
  if (old_advert_interval != _prefs->advert_interval) {
    _callbacks->updateAdvertTimer();
  }
  _callbacks->savePrefs();
}

uint8_t CommonCLI::buildAdvertData(uint8_t node_type, uint8_t* app_data) {
  if (_prefs->advert_loc_policy == ADVERT_LOC_NONE) {
    AdvertDataBuilder builder(node_type, _prefs->node_name);
    return builder.encodeTo(app_data);
  } else if (_prefs->advert_loc_policy == ADVERT_LOC_SHARE) {
    AdvertDataBuilder builder(node_type, _prefs->node_name, _sensors->node_lat, _sensors->node_lon);
    return builder.encodeTo(app_data);
  } else {
    AdvertDataBuilder builder(node_type, _prefs->node_name, _prefs->node_lat, _prefs->node_lon);
    return builder.encodeTo(app_data);
  }
}

void CommonCLI::handleCommand(uint32_t sender_timestamp, const char* command, char* reply) {
    if (memcmp(command, "reboot", 6) == 0) {
      _board->reboot();  // doesn't return
    } else if (memcmp(command, "clkreboot", 9) == 0) {
      // Reset clock
      getRTCClock()->setCurrentTime(1715770351);  // 15 May 2024, 8:50pm
      _board->reboot();  // doesn't return
    } else if (memcmp(command, "advert", 6) == 0) {
      // send flood advert
      _callbacks->sendSelfAdvertisement(1500, true);  // longer delay, give CLI response time to be sent first
      strcpy(reply, "OK - Advert sent");
    } else if (memcmp(command, "clock sync", 10) == 0) {
      uint32_t curr = getRTCClock()->getCurrentTime();
      if (sender_timestamp > curr) {
        getRTCClock()->setCurrentTime(sender_timestamp + 1);
        uint32_t now = getRTCClock()->getCurrentTime();
        DateTime dt = DateTime(now);
        sprintf(reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
      } else {
        strcpy(reply, "ERR: clock cannot go backwards");
      }
    } else if (memcmp(command, "memory", 6) == 0) {
      sprintf(reply, "Free: %d, Min: %d, Max: %d, Queue: %d", 
              ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), 
              _callbacks->getQueueSize());
    } else if (memcmp(command, "start ota", 9) == 0) {
      if (!_board->startOTAUpdate(_prefs->node_name, reply)) {
        strcpy(reply, "Error");
      }
    } else if (memcmp(command, "clock", 5) == 0) {
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      sprintf(reply, "%02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else if (memcmp(command, "time ", 5) == 0) {  // set time (to epoch seconds)
      uint32_t secs = _atoi(&command[5]);
      uint32_t curr = getRTCClock()->getCurrentTime();
      if (secs > curr) {
        getRTCClock()->setCurrentTime(secs);
        uint32_t now = getRTCClock()->getCurrentTime();
        DateTime dt = DateTime(now);
        sprintf(reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
      } else {
        strcpy(reply, "(ERR: clock cannot go backwards)");
      }
    } else if (memcmp(command, "neighbors", 9) == 0) {
      _callbacks->formatNeighborsReply(reply);
    } else if (memcmp(command, "neighbor.remove ", 16) == 0) {
      const char* hex = &command[16];
      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min((int)strlen(hex), PUB_KEY_SIZE*2);
      int pubkey_len = hex_len / 2;
      if (mesh::Utils::fromHex(pubkey, pubkey_len, hex)) {
        _callbacks->removeNeighbor(pubkey, pubkey_len);
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "ERR: bad pubkey");
      }
    } else if (memcmp(command, "tempradio ", 10) == 0) {
      strcpy(tmp, &command[10]);
      const char *parts[5];
      int num = mesh::Utils::parseTextParts(tmp, parts, 5);
      float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
      float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
      uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
      uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
      int temp_timeout_mins  = num > 4 ? atoi(parts[4]) : 0;
      if (freq >= 300.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
        _callbacks->applyTempRadioParams(freq, bw, sf, cr, temp_timeout_mins);
        sprintf(reply, "OK - temp params for %d mins", temp_timeout_mins);
      } else {
        strcpy(reply, "Error, invalid params");
      }
    } else if (memcmp(command, "password ", 9) == 0) {
      // change admin password
      StrHelper::strncpy(_prefs->password, &command[9], sizeof(_prefs->password));
      savePrefs();
      sprintf(reply, "password now: %s", _prefs->password);   // echo back just to let admin know for sure!!
    } else if (memcmp(command, "clear stats", 11) == 0) {
      _callbacks->clearStats();
      strcpy(reply, "(OK - stats reset)");
    /*
     * GET commands
     */
    } else if (memcmp(command, "get ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "af", 2) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->airtime_factor));
      } else if (memcmp(config, "int.thresh", 10) == 0) {
        sprintf(reply, "> %d", (uint32_t) _prefs->interference_threshold);
      } else if (memcmp(config, "agc.reset.interval", 18) == 0) {
        sprintf(reply, "> %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
      } else if (memcmp(config, "multi.acks", 10) == 0) {
        sprintf(reply, "> %d", (uint32_t) _prefs->multi_acks);
      } else if (memcmp(config, "allow.read.only", 15) == 0) {
        sprintf(reply, "> %s", _prefs->allow_read_only ? "on" : "off");
      } else if (memcmp(config, "flood.advert.interval", 21) == 0) {
        sprintf(reply, "> %d", ((uint32_t) _prefs->flood_advert_interval));
      } else if (memcmp(config, "advert.interval", 15) == 0) {
        sprintf(reply, "> %d", ((uint32_t) _prefs->advert_interval) * 2);
      } else if (memcmp(config, "guest.password", 14) == 0) {
        sprintf(reply, "> %s", _prefs->guest_password);
      } else if (sender_timestamp == 0 && memcmp(config, "prv.key", 7) == 0) {  // from serial command line only
        uint8_t prv_key[PRV_KEY_SIZE];
        int len = _callbacks->getSelfId().writeTo(prv_key, PRV_KEY_SIZE);
        mesh::Utils::toHex(tmp, prv_key, len);
        sprintf(reply, "> %s", tmp);
      } else if (memcmp(config, "name", 4) == 0) {
        sprintf(reply, "> %s", _prefs->node_name);
      } else if (memcmp(config, "repeat", 6) == 0) {
        sprintf(reply, "> %s", _prefs->disable_fwd ? "off" : "on");
      } else if (memcmp(config, "lat", 3) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lat));
      } else if (memcmp(config, "lon", 3) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->node_lon));
      } else if (memcmp(config, "radio", 5) == 0) {
        char freq[16], bw[16];
        strcpy(freq, StrHelper::ftoa(_prefs->freq));
        strcpy(bw, StrHelper::ftoa3(_prefs->bw));
        sprintf(reply, "> %s,%s,%d,%d", freq, bw, (uint32_t)_prefs->sf, (uint32_t)_prefs->cr);
      } else if (memcmp(config, "rxdelay", 7) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->rx_delay_base));
      } else if (memcmp(config, "txdelay", 7) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->tx_delay_factor));
      } else if (memcmp(config, "flood.max", 9) == 0) {
        sprintf(reply, "> %d", (uint32_t)_prefs->flood_max);
      } else if (memcmp(config, "direct.txdelay", 14) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->direct_tx_delay_factor));
      } else if (memcmp(config, "owner.info", 10) == 0) {
        *reply++ = '>';
        *reply++ = ' ';
        const char* sp = _prefs->owner_info;
        while (*sp) {
          *reply++ = (*sp == '\n') ? '|' : *sp;    // translate newline back to orig '|'
          sp++;
        }
        *reply = 0;  // set null terminator
      } else if (memcmp(config, "tx", 2) == 0 && (config[2] == 0 || config[2] == ' ')) {
        sprintf(reply, "> %d", (uint32_t) _prefs->tx_power_dbm);
      } else if (memcmp(config, "freq", 4) == 0) {
        sprintf(reply, "> %s", StrHelper::ftoa(_prefs->freq));
      } else if (memcmp(config, "public.key", 10) == 0) {
        strcpy(reply, "> ");
        mesh::Utils::toHex(&reply[2], _callbacks->getSelfId().pub_key, PUB_KEY_SIZE);
      } else if (memcmp(config, "role", 4) == 0) {
        sprintf(reply, "> %s", _callbacks->getRole());
      } else if (memcmp(config, "bridge.type", 11) == 0) {
        sprintf(reply, "> %s",
#ifdef WITH_RS232_BRIDGE
                "rs232"
#elif WITH_ESPNOW_BRIDGE
                "espnow"
#else
                "none"
#endif
        );
#ifdef WITH_BRIDGE
      } else if (memcmp(config, "bridge.enabled", 14) == 0) {
        sprintf(reply, "> %s", _prefs->bridge_enabled ? "on" : "off");
      } else if (memcmp(config, "bridge.delay", 12) == 0) {
        sprintf(reply, "> %d", (uint32_t)_prefs->bridge_delay);
      } else if (memcmp(config, "bridge.source", 13) == 0) {
        sprintf(reply, "> %s", _prefs->bridge_pkt_src ? "logRx" : "logTx");
#endif
#ifdef WITH_RS232_BRIDGE
      } else if (memcmp(config, "bridge.baud", 11) == 0) {
        sprintf(reply, "> %d", (uint32_t)_prefs->bridge_baud);
#endif
#ifdef WITH_ESPNOW_BRIDGE
      } else if (memcmp(config, "bridge.channel", 14) == 0) {
        sprintf(reply, "> %d", (uint32_t)_prefs->bridge_channel);
      } else if (memcmp(config, "bridge.secret", 13) == 0) {
        sprintf(reply, "> %s", _prefs->bridge_secret);
#endif
#ifdef WITH_MQTT_BRIDGE
      } else if (memcmp(config, "mqtt.origin", 11) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_origin);
      } else if (memcmp(config, "mqtt.iata", 9) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_iata);
      } else if (memcmp(config, "mqtt.status", 11) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_status_enabled ? "on" : "off");
      } else if (memcmp(config, "mqtt.packets", 12) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_packets_enabled ? "on" : "off");
      } else if (memcmp(config, "mqtt.raw", 8) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_raw_enabled ? "on" : "off");
      } else if (memcmp(config, "mqtt.tx", 7) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_tx_enabled ? "on" : "off");
      } else if (memcmp(config, "mqtt.interval", 13) == 0) {
        // Display interval in minutes (rounded)
        uint32_t minutes = (_prefs->mqtt_status_interval + 29999) / 60000; // Round up
        sprintf(reply, "> %u minutes (%lu ms)", minutes, _prefs->mqtt_status_interval);
      } else if (memcmp(config, "mqtt.server", 11) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_server);
      } else if (memcmp(config, "mqtt.port", 9) == 0) {
        sprintf(reply, "> %d", _prefs->mqtt_port);
      } else if (memcmp(config, "mqtt.username", 13) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_username);
      } else if (memcmp(config, "mqtt.password", 13) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_password);
      } else if (memcmp(config, "wifi.ssid", 9) == 0) {
        sprintf(reply, "> %s", _prefs->wifi_ssid);
      } else if (memcmp(config, "wifi.pwd", 8) == 0) {
        sprintf(reply, "> %s", _prefs->wifi_password);
      } else if (memcmp(config, "wifi.status", 11) == 0) {
        wl_status_t status = WiFi.status();
        const char* status_str;
        switch(status) {
          case WL_CONNECTED: status_str = "connected"; break;
          case WL_NO_SSID_AVAIL: status_str = "no_ssid"; break;
          case WL_CONNECT_FAILED: status_str = "connect_failed"; break;
          case WL_CONNECTION_LOST: status_str = "connection_lost"; break;
          case WL_DISCONNECTED: status_str = "disconnected"; break;
          default: status_str = "unknown"; break;
        }
        if (status == WL_CONNECTED) {
          sprintf(reply, "> %s, IP: %s, RSSI: %d dBm", status_str, WiFi.localIP().toString().c_str(), WiFi.RSSI());
        } else {
          sprintf(reply, "> %s (code: %d)", status_str, status);
        }
      } else if (memcmp(config, "wifi.powersave", 14) == 0) {
        uint8_t ps = _prefs->wifi_power_save;
        const char* ps_name = (ps == 1) ? "none" : (ps == 2) ? "max" : "min";
        sprintf(reply, "> %s", ps_name);
      } else if (memcmp(config, "timezone", 8) == 0) {
        sprintf(reply, "> %s", _prefs->timezone_string);
      } else if (memcmp(config, "timezone.offset", 15) == 0) {
        sprintf(reply, "> %d", _prefs->timezone_offset);
      } else if (memcmp(config, "mqtt.analyzer.us", 17) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_analyzer_us_enabled ? "on" : "off");
      } else if (memcmp(config, "mqtt.analyzer.eu", 17) == 0) {
        sprintf(reply, "> %s", _prefs->mqtt_analyzer_eu_enabled ? "on" : "off");
      } else if (sender_timestamp == 0 && memcmp(config, "mqtt.owner", 10) == 0) {  // from serial command line only
        if (_prefs->mqtt_owner_public_key[0] != '\0') {
          sprintf(reply, "> %s", _prefs->mqtt_owner_public_key);
        } else {
          strcpy(reply, "> (not set)");
        }
      } else if (sender_timestamp == 0 && memcmp(config, "mqtt.email", 10) == 0) {  // from serial command line only
        if (_prefs->mqtt_email[0] != '\0') {
          sprintf(reply, "> %s", _prefs->mqtt_email);
        } else {
          strcpy(reply, "> (not set)");
        }
      } else if (memcmp(config, "mqtt.config.valid", 17) == 0) {
        bool valid = MQTTBridge::isConfigValid(_prefs);
        sprintf(reply, "> %s", valid ? "valid" : "invalid");
#endif
      } else if (memcmp(config, "adc.multiplier", 14) == 0) {
        float adc_mult = _board->getAdcMultiplier();
        if (adc_mult == 0.0f) {
          strcpy(reply, "Error: unsupported by this board");
        } else {
          sprintf(reply, "> %.3f", adc_mult);
        }
      // Power management commands
      } else if (memcmp(config, "pwrmgt.support", 14) == 0) {
#ifdef NRF52_POWER_MANAGEMENT
        strcpy(reply, "> supported");
#else
        strcpy(reply, "> unsupported");
#endif
      } else if (memcmp(config, "pwrmgt.source", 13) == 0) {
#ifdef NRF52_POWER_MANAGEMENT
        strcpy(reply, _board->isExternalPowered() ? "> external" : "> battery");
#else
        strcpy(reply, "ERROR: Power management not supported");
#endif
      } else if (memcmp(config, "pwrmgt.bootreason", 17) == 0) {
#ifdef NRF52_POWER_MANAGEMENT
        sprintf(reply, "> Reset: %s; Shutdown: %s",
          _board->getResetReasonString(_board->getResetReason()),
          _board->getShutdownReasonString(_board->getShutdownReason()));
#else
        strcpy(reply, "ERROR: Power management not supported");
#endif
      } else if (memcmp(config, "pwrmgt.bootmv", 13) == 0) {
#ifdef NRF52_POWER_MANAGEMENT
        sprintf(reply, "> %u mV", _board->getBootVoltage());
#else
        strcpy(reply, "ERROR: Power management not supported");
#endif
      } else {
        sprintf(reply, "??: %s", config);
      }
    /*
     * SET commands
     */
    } else if (memcmp(command, "set ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "af ", 3) == 0) {
        _prefs->airtime_factor = atof(&config[3]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "int.thresh ", 11) == 0) {
        _prefs->interference_threshold = atoi(&config[11]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "agc.reset.interval ", 19) == 0) {
        _prefs->agc_reset_interval = atoi(&config[19]) / 4;
        savePrefs();
        sprintf(reply, "OK - interval rounded to %d", ((uint32_t) _prefs->agc_reset_interval) * 4);
      } else if (memcmp(config, "multi.acks ", 11) == 0) {
        _prefs->multi_acks = atoi(&config[11]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "allow.read.only ", 16) == 0) {
        _prefs->allow_read_only = memcmp(&config[16], "on", 2) == 0;
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "flood.advert.interval ", 22) == 0) {
        int hours = _atoi(&config[22]);
        if ((hours > 0 && hours < 3) || (hours > 168)) {
          strcpy(reply, "Error: interval range is 3-168 hours");
        } else {
          _prefs->flood_advert_interval = (uint8_t)(hours);
          _callbacks->updateFloodAdvertTimer();
          savePrefs();
          strcpy(reply, "OK");
        }
      } else if (memcmp(config, "advert.interval ", 16) == 0) {
        int mins = _atoi(&config[16]);
        if ((mins > 0 && mins < MIN_LOCAL_ADVERT_INTERVAL) || (mins > 240)) {
          sprintf(reply, "Error: interval range is %d-240 minutes", MIN_LOCAL_ADVERT_INTERVAL);
        } else {
          _prefs->advert_interval = (uint8_t)(mins / 2);
          _callbacks->updateAdvertTimer();
          savePrefs();
          strcpy(reply, "OK");
        }
      } else if (memcmp(config, "guest.password ", 15) == 0) {
        StrHelper::strncpy(_prefs->guest_password, &config[15], sizeof(_prefs->guest_password));
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "prv.key ", 8) == 0) {
        uint8_t prv_key[PRV_KEY_SIZE];
        bool success = mesh::Utils::fromHex(prv_key, PRV_KEY_SIZE, &config[8]);
        // only allow rekey if key is valid
        if (success && mesh::LocalIdentity::validatePrivateKey(prv_key)) {
          mesh::LocalIdentity new_id;
          new_id.readFrom(prv_key, PRV_KEY_SIZE);
          _callbacks->saveIdentity(new_id);
          strcpy(reply, "OK, reboot to apply! New pubkey: ");
          mesh::Utils::toHex(&reply[33], new_id.pub_key, PUB_KEY_SIZE);
        } else {
          strcpy(reply, "Error, bad key");
        }
      } else if (memcmp(config, "name ", 5) == 0) {
        if (isValidName(&config[5])) {
          StrHelper::strncpy(_prefs->node_name, &config[5], sizeof(_prefs->node_name));
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, bad chars");
        }
      } else if (memcmp(config, "repeat ", 7) == 0) {
        _prefs->disable_fwd = memcmp(&config[7], "off", 3) == 0;
        savePrefs();
        strcpy(reply, _prefs->disable_fwd ? "OK - repeat is now OFF" : "OK - repeat is now ON");
      } else if (memcmp(config, "radio ", 6) == 0) {
        strcpy(tmp, &config[6]);
        const char *parts[4];
        int num = mesh::Utils::parseTextParts(tmp, parts, 4);
        float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
        float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
        uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
        uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
        if (freq >= 300.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
          _prefs->sf = sf;
          _prefs->cr = cr;
          _prefs->freq = freq;
          _prefs->bw = bw;
          _callbacks->savePrefs();
          strcpy(reply, "OK - reboot to apply");
        } else {
          strcpy(reply, "Error, invalid radio params");
        }
      } else if (memcmp(config, "lat ", 4) == 0) {
        _prefs->node_lat = atof(&config[4]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "lon ", 4) == 0) {
        _prefs->node_lon = atof(&config[4]);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "rxdelay ", 8) == 0) {
        float db = atof(&config[8]);
        if (db >= 0) {
          _prefs->rx_delay_base = db;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, "txdelay ", 8) == 0) {
        float f = atof(&config[8]);
        if (f >= 0) {
          _prefs->tx_delay_factor = f;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, "flood.max ", 10) == 0) {
        uint8_t m = atoi(&config[10]);
        if (m <= 64) {
          _prefs->flood_max = m;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, max 64");
        }
      } else if (memcmp(config, "direct.txdelay ", 15) == 0) {
        float f = atof(&config[15]);
        if (f >= 0) {
          _prefs->direct_tx_delay_factor = f;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error, cannot be negative");
        }
      } else if (memcmp(config, "owner.info ", 11) == 0) {
        config += 11;
        char *dp = _prefs->owner_info;
        while (*config && dp - _prefs->owner_info < sizeof(_prefs->owner_info)-1) {
          *dp++ = (*config == '|') ? '\n' : *config;    // translate '|' to newline chars
          config++;
        }
        *dp = 0;
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "tx ", 3) == 0) {
        _prefs->tx_power_dbm = atoi(&config[3]);
        savePrefs();
        _callbacks->setTxPower(_prefs->tx_power_dbm);
        strcpy(reply, "OK");
      } else if (sender_timestamp == 0 && memcmp(config, "freq ", 5) == 0) {
        _prefs->freq = atof(&config[5]);
        savePrefs();
        strcpy(reply, "OK - reboot to apply");
#ifdef WITH_BRIDGE
      } else if (memcmp(config, "bridge.enabled ", 15) == 0) {
        _prefs->bridge_enabled = memcmp(&config[15], "on", 2) == 0;
        _callbacks->setBridgeState(_prefs->bridge_enabled);
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "bridge.delay ", 13) == 0) {
        int delay = _atoi(&config[13]);
        if (delay >= 0 && delay <= 10000) {
          _prefs->bridge_delay = (uint16_t)delay;
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error: delay must be between 0-10000 ms");
        }
      } else if (memcmp(config, "bridge.source ", 14) == 0) {
        _prefs->bridge_pkt_src = memcmp(&config[14], "rx", 2) == 0;
        savePrefs();
        strcpy(reply, "OK");
#endif
#ifdef WITH_RS232_BRIDGE
      } else if (memcmp(config, "bridge.baud ", 12) == 0) {
        uint32_t baud = atoi(&config[12]);
        if (baud >= 9600 && baud <= 115200) {
          _prefs->bridge_baud = (uint32_t)baud;
          _callbacks->restartBridge();
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error: baud rate must be between 9600-115200");
        }
#endif
#ifdef WITH_ESPNOW_BRIDGE
      } else if (memcmp(config, "bridge.channel ", 15) == 0) {
        int ch = atoi(&config[15]);
        if (ch > 0 && ch < 15) {
          _prefs->bridge_channel = (uint8_t)ch;
          _callbacks->restartBridge();
          savePrefs();
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Error: channel must be between 1-14");
        }
      } else if (memcmp(config, "bridge.secret ", 14) == 0) {
        StrHelper::strncpy(_prefs->bridge_secret, &config[14], sizeof(_prefs->bridge_secret));
        _callbacks->restartBridge();
        savePrefs();
        strcpy(reply, "OK");
#endif
#ifdef WITH_MQTT_BRIDGE
      } else if (memcmp(config, "mqtt.origin ", 12) == 0) {
        StrHelper::strncpy(_prefs->mqtt_origin, &config[12], sizeof(_prefs->mqtt_origin));
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "mqtt.iata ", 10) == 0) {
        StrHelper::strncpy(_prefs->mqtt_iata, &config[10], sizeof(_prefs->mqtt_iata));
        // Convert IATA code to uppercase (IATA codes are conventionally uppercase)
        for (int i = 0; _prefs->mqtt_iata[i]; i++) {
          _prefs->mqtt_iata[i] = toupper(_prefs->mqtt_iata[i]);
        }
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "mqtt.status ", 12) == 0) {
        _prefs->mqtt_status_enabled = memcmp(&config[12], "on", 2) == 0;
        savePrefs();
        strcpy(reply, "OK");
      } else if (memcmp(config, "mqtt.packets ", 13) == 0) {
        _prefs->mqtt_packets_enabled = memcmp(&config[13], "on", 2) == 0;
        savePrefs();
        strcpy(reply, "OK");
              } else if (memcmp(config, "mqtt.raw ", 9) == 0) {
                _prefs->mqtt_raw_enabled = memcmp(&config[9], "on", 2) == 0;
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "mqtt.tx ", 8) == 0) {
                _prefs->mqtt_tx_enabled = memcmp(&config[8], "on", 2) == 0;
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "mqtt.interval ", 14) == 0) {
                uint32_t minutes = _atoi(&config[14]);
                if (minutes >= 1 && minutes <= 60) { // 1 minute to 60 minutes
                  _prefs->mqtt_status_interval = minutes * 60000; // Convert minutes to milliseconds
                  savePrefs();
                  // Restart bridge to pick up new interval value
                  _callbacks->restartBridge();
                  sprintf(reply, "OK - interval set to %u minutes (%lu ms), bridge restarted", minutes, _prefs->mqtt_status_interval);
                } else {
                  strcpy(reply, "Error: interval must be between 1-60 minutes");
                }
              } else if (memcmp(config, "wifi.ssid ", 10) == 0) {
                StrHelper::strncpy(_prefs->wifi_ssid, &config[10], sizeof(_prefs->wifi_ssid));
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "wifi.pwd ", 9) == 0) {
                StrHelper::strncpy(_prefs->wifi_password, &config[9], sizeof(_prefs->wifi_password));
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "wifi.powersave ", 15) == 0) {
                const char* value = &config[15];
                uint8_t ps_value;
                bool valid = false;
                if (memcmp(value, "min", 3) == 0 && (value[3] == 0 || value[3] == ' ')) {
                  ps_value = 0;
                  valid = true;
                } else if (memcmp(value, "none", 4) == 0 && (value[4] == 0 || value[4] == ' ')) {
                  ps_value = 1;
                  valid = true;
                } else if (memcmp(value, "max", 3) == 0 && (value[3] == 0 || value[3] == ' ')) {
                  ps_value = 2;
                  valid = true;
                }
                
                if (!valid) {
                  strcpy(reply, "Error: must be none, min, or max");
                } else {
                  _prefs->wifi_power_save = ps_value;
                  savePrefs();
                  
                  // Apply immediately if WiFi is connected
                  #ifdef ESP_PLATFORM
                  if (WiFi.status() == WL_CONNECTED) {
                    wifi_ps_type_t ps_mode = (ps_value == 1) ? WIFI_PS_NONE : 
                                            (ps_value == 2) ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM;
                    esp_err_t ps_result = esp_wifi_set_ps(ps_mode);
                    if (ps_result == ESP_OK) {
                      const char* ps_name = (ps_value == 1) ? "none" : (ps_value == 2) ? "max" : "min";
                      sprintf(reply, "OK - power save set to %s", ps_name);
                    } else {
                      sprintf(reply, "OK - saved, but failed to apply: %d", ps_result);
                    }
                  } else {
                    const char* ps_name = (ps_value == 1) ? "none" : (ps_value == 2) ? "max" : "min";
                    sprintf(reply, "OK - saved as %s (will apply on next WiFi connection)", ps_name);
                  }
                  #else
                  const char* ps_name = (ps_value == 1) ? "none" : (ps_value == 2) ? "max" : "min";
                  sprintf(reply, "OK - saved as %s", ps_name);
                  #endif
                }
              } else if (memcmp(config, "timezone ", 9) == 0) {
                StrHelper::strncpy(_prefs->timezone_string, &config[9], sizeof(_prefs->timezone_string));
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "timezone.offset ", 16) == 0) {
                int8_t offset = _atoi(&config[16]);
                if (offset >= -12 && offset <= 14) {
                  _prefs->timezone_offset = offset;
                  savePrefs();
                  strcpy(reply, "OK");
                } else {
                  strcpy(reply, "Error: timezone offset must be between -12 and +14");
                }
              } else if (memcmp(config, "mqtt.server ", 12) == 0) {
                StrHelper::strncpy(_prefs->mqtt_server, &config[12], sizeof(_prefs->mqtt_server));
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "mqtt.port ", 10) == 0) {
                int port = atoi(&config[10]);
                if (port > 0 && port <= 65535) {
                  _prefs->mqtt_port = port;
                  savePrefs();
                  strcpy(reply, "OK");
                } else {
                  strcpy(reply, "Error: port must be between 1 and 65535");
                }
              } else if (memcmp(config, "mqtt.username ", 14) == 0) {
                StrHelper::strncpy(_prefs->mqtt_username, &config[14], sizeof(_prefs->mqtt_username));
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "mqtt.password ", 14) == 0) {
                StrHelper::strncpy(_prefs->mqtt_password, &config[14], sizeof(_prefs->mqtt_password));
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "mqtt.analyzer.us ", 17) == 0) {
                _prefs->mqtt_analyzer_us_enabled = memcmp(&config[17], "on", 2) == 0;
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "mqtt.analyzer.eu ", 17) == 0) {
                _prefs->mqtt_analyzer_eu_enabled = memcmp(&config[17], "on", 2) == 0;
                savePrefs();
                strcpy(reply, "OK");
              } else if (memcmp(config, "mqtt.owner ", 11) == 0) {
                // Validate that it's a valid hex string of the correct length (64 hex chars = 32 bytes)
                const char* owner_key = &config[11];
                int key_len = strlen(owner_key);
                if (key_len == 64) {
                  // Validate hex characters
                  bool valid = true;
                  for (int i = 0; i < key_len; i++) {
                    if (!((owner_key[i] >= '0' && owner_key[i] <= '9') ||
                          (owner_key[i] >= 'A' && owner_key[i] <= 'F') ||
                          (owner_key[i] >= 'a' && owner_key[i] <= 'f'))) {
                      valid = false;
                      break;
                    }
                  }
                  if (valid) {
                    StrHelper::strncpy(_prefs->mqtt_owner_public_key, owner_key, sizeof(_prefs->mqtt_owner_public_key));
                    savePrefs();
                    strcpy(reply, "OK");
                  } else {
                    strcpy(reply, "Error: invalid hex characters in public key");
                  }
                } else {
                  strcpy(reply, "Error: public key must be 64 hex characters (32 bytes)");
                }
              } else if (memcmp(config, "mqtt.email ", 11) == 0) {
                StrHelper::strncpy(_prefs->mqtt_email, &config[11], sizeof(_prefs->mqtt_email));
                savePrefs();
                strcpy(reply, "OK");
#endif
      } else {
        sprintf(reply, "unknown config: %s", config);
      }
    } else if (sender_timestamp == 0 && strcmp(command, "erase") == 0) {
      bool s = _callbacks->formatFileSystem();
      sprintf(reply, "File system erase: %s", s ? "OK" : "Err");
    } else if (memcmp(command, "ver", 3) == 0) {
      sprintf(reply, "%s (Build: %s)", _callbacks->getFirmwareVer(), _callbacks->getBuildDate());
    } else if (memcmp(command, "board", 5) == 0) {
      sprintf(reply, "%s", _board->getManufacturerName());
    } else if (memcmp(command, "sensor get ", 11) == 0) {
      const char* key = command + 11;
      const char* val = _sensors->getSettingByKey(key);
      if (val != NULL) {
        sprintf(reply, "> %s", val);
      } else {
        strcpy(reply, "null");
      }
    } else if (memcmp(command, "sensor set ", 11) == 0) {
      strcpy(tmp, &command[11]);
      const char *parts[2]; 
      int num = mesh::Utils::parseTextParts(tmp, parts, 2, ' ');
      const char *key = (num > 0) ? parts[0] : "";
      const char *value = (num > 1) ? parts[1] : "null";
      if (_sensors->setSettingValue(key, value)) {
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "can't find custom var");
      }
    } else if (memcmp(command, "sensor list", 11) == 0) {
      char* dp = reply;
      int start = 0;
      int end = _sensors->getNumSettings();
      if (strlen(command) > 11) {
        start = _atoi(command+12);
      }
      if (start >= end) {
        strcpy(reply, "no custom var");
      } else {
        sprintf(dp, "%d vars\n", end);
        dp = strchr(dp, 0);
        int i;
        for (i = start; i < end && (dp-reply < 134); i++) {
          sprintf(dp, "%s=%s\n", 
            _sensors->getSettingName(i),
            _sensors->getSettingValue(i));
          dp = strchr(dp, 0);
        }
        if (i < end) {
          sprintf(dp, "... next:%d", i);
        } else {
          *(dp-1) = 0; // remove last CR
        }
      }
#if ENV_INCLUDE_GPS == 1
    } else if (memcmp(command, "gps on", 6) == 0) {
      if (_sensors->setSettingValue("gps", "1")) {
        _prefs->gps_enabled = 1;
        savePrefs();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "gps toggle not found");
      }
    } else if (memcmp(command, "gps off", 7) == 0) {
      if (_sensors->setSettingValue("gps", "0")) {
        _prefs->gps_enabled = 0;
        savePrefs();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "gps toggle not found");
      }
    } else if (memcmp(command, "gps sync", 8) == 0) {
      LocationProvider * l = _sensors->getLocationProvider();
      if (l != NULL) {
        l->syncTime();
      }
    } else if (memcmp(command, "gps setloc", 10) == 0) {
      _prefs->node_lat = _sensors->node_lat;
      _prefs->node_lon = _sensors->node_lon;
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "gps advert", 10) == 0) {
      if (strlen(command) == 10) {
        switch (_prefs->advert_loc_policy) {
          case ADVERT_LOC_NONE:
            strcpy(reply, "> none");
            break;
          case ADVERT_LOC_PREFS:
            strcpy(reply, "> prefs");
            break;
          case ADVERT_LOC_SHARE:
            strcpy(reply, "> share");
            break;
          default:
            strcpy(reply, "error");
        }
      } else if (memcmp(command+11, "none", 4) == 0) {
        _prefs->advert_loc_policy = ADVERT_LOC_NONE;
        savePrefs();
        strcpy(reply, "ok");
      } else if (memcmp(command+11, "share", 5) == 0) {
        _prefs->advert_loc_policy = ADVERT_LOC_SHARE;
        savePrefs();
        strcpy(reply, "ok");
      } else if (memcmp(command+11, "prefs", 4) == 0) {
        _prefs->advert_loc_policy = ADVERT_LOC_PREFS;
        savePrefs();
        strcpy(reply, "ok");
      } else {
        strcpy(reply, "error");
      }
    } else if (memcmp(command, "gps", 3) == 0) {
      LocationProvider * l = _sensors->getLocationProvider();
      if (l != NULL) {
        bool enabled = l->isEnabled(); // is EN pin on ?
        bool fix = l->isValid();       // has fix ?
        int sats = l->satellitesCount();
        bool active = !strcmp(_sensors->getSettingByKey("gps"), "1");
        if (enabled) {
          sprintf(reply, "on, %s, %s, %d sats",
            active?"active":"deactivated", 
            fix?"fix":"no fix", 
            sats);
        } else {
          strcpy(reply, "off");
        }
      } else {
        strcpy(reply, "Can't find GPS");
      }
#endif
    } else if (memcmp(command, "powersaving on", 14) == 0) {
      _prefs->powersaving_enabled = 1;
      savePrefs();
      strcpy(reply, "ok"); // TODO: to return Not supported if required
    } else if (memcmp(command, "powersaving off", 15) == 0) {
      _prefs->powersaving_enabled = 0;
      savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command, "powersaving", 11) == 0) {
      if (_prefs->powersaving_enabled) {
        strcpy(reply, "on");
      } else {
        strcpy(reply, "off");
      }
    } else if (memcmp(command, "log start", 9) == 0) {
      _callbacks->setLoggingOn(true);
      strcpy(reply, "   logging on");
    } else if (memcmp(command, "log stop", 8) == 0) {
      _callbacks->setLoggingOn(false);
      strcpy(reply, "   logging off");
    } else if (memcmp(command, "log erase", 9) == 0) {
      _callbacks->eraseLogFile();
      strcpy(reply, "   log erased");
    } else if (sender_timestamp == 0 && memcmp(command, "log", 3) == 0) {
      _callbacks->dumpLogFile();
      strcpy(reply, "   EOF");
    } else if (sender_timestamp == 0 && memcmp(command, "stats-packets", 13) == 0 && (command[13] == 0 || command[13] == ' ')) {
      _callbacks->formatPacketStatsReply(reply);
    } else if (sender_timestamp == 0 && memcmp(command, "stats-radio", 11) == 0 && (command[11] == 0 || command[11] == ' ')) {
      _callbacks->formatRadioStatsReply(reply);
    } else if (sender_timestamp == 0 && memcmp(command, "stats-core", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
      _callbacks->formatStatsReply(reply);
    } else {
      strcpy(reply, "Unknown command");
    }
}
