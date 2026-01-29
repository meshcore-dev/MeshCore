#pragma once

#include "Mesh.h"
#include <helpers/IdentityStore.h>
#include <helpers/SensorManager.h>
#include <helpers/ClientACL.h>

#if defined(WITH_RS232_BRIDGE) || defined(WITH_ESPNOW_BRIDGE) || defined(WITH_MQTT_BRIDGE)
#define WITH_BRIDGE
#endif

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1
#define ADVERT_LOC_PREFS      2

struct NodePrefs { // persisted to file
  float airtime_factor;
  char node_name[32];
  double node_lat, node_lon;
  char password[16];
  float freq;
  uint8_t tx_power_dbm;
  uint8_t disable_fwd;
  uint8_t advert_interval;       // minutes / 2
  uint8_t flood_advert_interval; // hours
  float rx_delay_base;
  float tx_delay_factor;
  char guest_password[16];
  float direct_tx_delay_factor;
  uint32_t guard;
  uint8_t sf;
  uint8_t cr;
  uint8_t allow_read_only;
  uint8_t multi_acks;
  float bw;
  uint8_t flood_max;
  uint8_t interference_threshold;
  uint8_t agc_reset_interval; // secs / 4
  // Bridge settings
  uint8_t bridge_enabled; // boolean
  uint16_t bridge_delay;  // milliseconds (default 500 ms)
  uint8_t bridge_pkt_src; // 0 = logTx, 1 = logRx (default logRx)
  uint32_t bridge_baud;   // 9600, 19200, 38400, 57600, 115200 (default 115200)
  uint8_t bridge_channel; // 1-14 (ESP-NOW only)
  char bridge_secret[16]; // for XOR encryption of bridge packets (ESP-NOW only)
  // Power setting
  uint8_t powersaving_enabled; // boolean
  // Gps settings
  uint8_t gps_enabled;
  uint32_t gps_interval; // in seconds
  uint8_t advert_loc_policy;
  uint32_t discovery_mod_timestamp;
  float adc_multiplier;
  char owner_info[120];
  // MQTT settings (stored separately in /mqtt_prefs, but kept here for backward compatibility)
  char mqtt_origin[32];     // Device name for MQTT topics
  char mqtt_iata[8];        // IATA code for MQTT topics
  uint8_t mqtt_status_enabled;   // Enable status messages
  uint8_t mqtt_packets_enabled;  // Enable packet messages
  uint8_t mqtt_raw_enabled;      // Enable raw messages
  uint8_t mqtt_tx_enabled;       // Enable TX packet uplinking
  uint32_t mqtt_status_interval; // Status publish interval (ms)
  
  // WiFi settings
  char wifi_ssid[32];       // WiFi SSID
  char wifi_password[64];  // WiFi password
  uint8_t wifi_power_save; // WiFi power save mode: 0=min, 1=none, 2=max (default: 0=min)
  
  // Timezone settings
  char timezone_string[32]; // Timezone string (e.g., "America/Los_Angeles")
  int8_t timezone_offset;   // Timezone offset in hours (-12 to +14) - fallback
  
  // MQTT server settings
  char mqtt_server[64];     // MQTT server hostname
  uint16_t mqtt_port;       // MQTT server port
  char mqtt_username[32];   // MQTT username
  char mqtt_password[64];   // MQTT password
  
  // Let's Mesh Analyzer settings
  uint8_t mqtt_analyzer_us_enabled; // Enable US analyzer server
  uint8_t mqtt_analyzer_eu_enabled; // Enable EU analyzer server
  char mqtt_owner_public_key[65]; // Owner public key (hex string, same length as repeater public key)
  char mqtt_email[64]; // Owner email address for matching nodes with owners
};

#ifdef WITH_MQTT_BRIDGE
// MQTT preferences stored in separate file to avoid conflicts with upstream NodePrefs changes
struct MQTTPrefs {
  // MQTT settings
  char mqtt_origin[32];     // Device name for MQTT topics
  char mqtt_iata[8];        // IATA code for MQTT topics
  uint8_t mqtt_status_enabled;   // Enable status messages
  uint8_t mqtt_packets_enabled;  // Enable packet messages
  uint8_t mqtt_raw_enabled;      // Enable raw messages
  uint8_t mqtt_tx_enabled;       // Enable TX packet uplinking
  uint32_t mqtt_status_interval; // Status publish interval (ms)
  
  // WiFi settings
  char wifi_ssid[32];       // WiFi SSID
  char wifi_password[64];  // WiFi password
  uint8_t wifi_power_save; // WiFi power save mode: 0=min, 1=none, 2=max (default: 0=min)
  
  // Timezone settings
  char timezone_string[32]; // Timezone string (e.g., "America/Los_Angeles")
  int8_t timezone_offset;   // Timezone offset in hours (-12 to +14) - fallback
  
  // MQTT server settings
  char mqtt_server[64];     // MQTT server hostname
  uint16_t mqtt_port;       // MQTT server port
  char mqtt_username[32];   // MQTT username
  char mqtt_password[64];   // MQTT password
  
  // Let's Mesh Analyzer settings
  uint8_t mqtt_analyzer_us_enabled; // Enable US analyzer server
  uint8_t mqtt_analyzer_eu_enabled; // Enable EU analyzer server
  char mqtt_owner_public_key[65]; // Owner public key (hex string, same length as repeater public key)
  char mqtt_email[64]; // Owner email address for matching nodes with owners
};
#endif

class CommonCLICallbacks {
public:
  virtual void savePrefs() = 0;
  virtual const char* getFirmwareVer() = 0;
  virtual const char* getBuildDate() = 0;
  virtual const char* getRole() = 0;
  virtual bool formatFileSystem() = 0;
  virtual void sendSelfAdvertisement(int delay_millis, bool flood) = 0;
  virtual void updateAdvertTimer() = 0;
  virtual void updateFloodAdvertTimer() = 0;
  virtual void setLoggingOn(bool enable) = 0;
  virtual void eraseLogFile() = 0;
  virtual void dumpLogFile() = 0;
  virtual void setTxPower(uint8_t power_dbm) = 0;
  virtual void formatNeighborsReply(char *reply) = 0;
  virtual void removeNeighbor(const uint8_t* pubkey, int key_len) {
    // no op by default
  };
  virtual void formatStatsReply(char *reply) = 0;
  virtual void formatRadioStatsReply(char *reply) = 0;
  virtual void formatPacketStatsReply(char *reply) = 0;
  virtual mesh::LocalIdentity& getSelfId() = 0;
  virtual void saveIdentity(const mesh::LocalIdentity& new_id) = 0;
  virtual void clearStats() = 0;
  virtual void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) = 0;

  virtual void setBridgeState(bool enable) {
    // no op by default
  };

  virtual void restartBridge() {
    // no op by default
  };

  virtual int getQueueSize() {
    return 0; // no op by default
  };
};

class CommonCLI {
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  CommonCLICallbacks* _callbacks;
  mesh::MainBoard* _board;
  SensorManager* _sensors;
  ClientACL* _acl;
  char tmp[PRV_KEY_SIZE*2 + 4];
#ifdef WITH_MQTT_BRIDGE
  MQTTPrefs _mqtt_prefs;
#endif

  mesh::RTCClock* getRTCClock() { return _rtc; }
  void savePrefs();
  void loadPrefsInt(FILESYSTEM* _fs, const char* filename);
#ifdef WITH_MQTT_BRIDGE
  void loadMQTTPrefs(FILESYSTEM* fs);
  void saveMQTTPrefs(FILESYSTEM* fs);
  void syncMQTTPrefsToNodePrefs();
  void syncNodePrefsToMQTTPrefs();
#endif

public:
  CommonCLI(mesh::MainBoard& board, mesh::RTCClock& rtc, SensorManager& sensors, ClientACL& acl, NodePrefs* prefs, CommonCLICallbacks* callbacks)
      : _board(&board), _rtc(&rtc), _sensors(&sensors), _acl(&acl), _prefs(prefs), _callbacks(callbacks) { }

  void loadPrefs(FILESYSTEM* _fs);
  void savePrefs(FILESYSTEM* _fs);
  void handleCommand(uint32_t sender_timestamp, const char* command, char* reply);
  mesh::MainBoard* getBoard() { return _board; }
  uint8_t buildAdvertData(uint8_t node_type, uint8_t* app_data);
};
