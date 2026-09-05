#pragma once

#include "Mesh.h"
#include <helpers/IdentityStore.h>
#include <helpers/SensorManager.h>
#include <helpers/ClientACL.h>
#include <helpers/MQTTPresets.h>  // For MAX_MQTT_SLOTS (used in NodePrefs struct layout)
#include <helpers/RegionMap.h>
#include <helpers/ConfigSerializer.h>

#if defined(WITH_RS232_BRIDGE) || defined(WITH_ESPNOW_BRIDGE) || defined(WITH_MQTT_BRIDGE)
#define WITH_BRIDGE
#endif

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1
#define ADVERT_LOC_PREFS      2

#define LOOP_DETECT_OFF       0
#define LOOP_DETECT_MINIMAL   1
#define LOOP_DETECT_MODERATE  2
#define LOOP_DETECT_STRICT    3

class NodePrefs : public ConfigSerializer {
public:
  // in-memory backing data
  float airtime_factor = 0;
  char node_name[32];
  double node_lat = 0, node_lon = 0;
  char password[16];
  float freq = 0;
  int8_t tx_power_dbm = 0;
  uint8_t disable_fwd = 0;
  uint8_t advert_interval = 0;       // minutes / 2
  uint8_t flood_advert_interval = 0; // hours
  float rx_delay_base = 0;
  float tx_delay_factor = 0;
  char guest_password[16];
  float direct_tx_delay_factor = 0;
  uint32_t guard = 0;
  uint8_t sf = 0;
  uint8_t cr = 0;
  uint8_t allow_read_only = 0;
  uint8_t multi_acks = 0;
  float bw = 0;
  uint8_t flood_max = 0;
  uint8_t flood_max_unscoped = 0;
  uint8_t flood_max_advert = 0;
  uint8_t interference_threshold = 0;
  uint8_t agc_reset_interval = 0; // secs / 4
  // Bridge settings
  uint8_t bridge_enabled = 0; // boolean
  uint16_t bridge_delay = 0;  // milliseconds (default 500 ms)
  uint8_t bridge_pkt_src = 0; // 0 = logTx, 1 = logRx (fresh installs default to logRx)
  uint32_t bridge_baud = 0;   // 9600, 19200, 38400, 57600, 115200 (default 115200)
  uint8_t bridge_channel = 0; // 1-14 (ESP-NOW only)
  char bridge_secret[16]; // for XOR encryption of bridge packets (ESP-NOW only)
  // Power setting
  uint8_t powersaving_enabled = 0; // boolean
  // Gps settings
  uint8_t gps_enabled = 0;
  uint32_t gps_interval = 0; // in seconds
  uint8_t advert_loc_policy = 0;
  uint32_t discovery_mod_timestamp = 0;
  float adc_multiplier = 0;
  char owner_info[120];
  uint8_t rx_boosted_gain = 0; // power settings
  uint8_t radio_fem_rxgain = 0; // LoRa FEM RX-gain (LNA); hardware driving is wired per-board
  uint8_t radio_fem_txgain = 0; // LoRa FEM TX gain setting
  uint8_t path_hash_mode = 0;   // which path mode to use when sending
  uint8_t loop_detect = 0;
  uint8_t cad_enabled = 0;      // hardware Channel Activity Detection before TX (boolean)
  uint8_t extra_sf[4];

  // NOTE: observer settings (MQTT/WiFi/timezone/SNMP/alert) are not in NodePrefs.
  // They live in MQTTPrefs, persisted separately to /mqtt_prefs, so this struct
  // stays aligned with upstream. See struct MQTTPrefs below.

private:
  class RadioPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("freq", _parent->freq);
      def("bw", _parent->bw);
      def("sf", _parent->sf);
      def("cr", _parent->cr);
      def("cad", _parent->cad_enabled);
      def("int_thr", _parent->interference_threshold);
      def("rxgain", _parent->rx_boosted_gain);
      def("fem_rxgain", _parent->radio_fem_rxgain);
      def("fem_txgain", _parent->radio_fem_txgain);
      def("tx", _parent->tx_power_dbm);
      def("af", _parent->airtime_factor);
      def("rxdelay", _parent->rx_delay_base);
      def("f_txdelay", _parent->tx_delay_factor);
      def("d_txdelay", _parent->direct_tx_delay_factor);
      def("agc_int", _parent->agc_reset_interval);
      def("hash_mode", _parent->path_hash_mode);
      def("multi_ack", _parent->multi_acks);
    }
  public:
    RadioPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RadioPrefs radio;

  class BridgePrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->bridge_enabled); // boolean
      def("delay", _parent->bridge_delay);  // milliseconds (default 500 ms)
      def("src", _parent->bridge_pkt_src); // 0 = logTx, 1 = logRx
      def("baud", _parent->bridge_baud);   // 9600, 19200, 38400, 57600, 115200 (default 115200)
      def("ch", _parent->bridge_channel); // 1-14 (ESP-NOW only)
      def("secret", _parent->bridge_secret, sizeof(_parent->bridge_secret)); // for XOR encryption of bridge packets (ESP-NOW only)
    }
  public:
    BridgePrefs(NodePrefs* parent) : _parent(parent) { }
  };
  BridgePrefs bridge;

  class GPSPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->gps_enabled); // boolean
      def("int", _parent->gps_interval);   // interval in seconds
      def("adv_loc", _parent->advert_loc_policy);
    }
  public:
    GPSPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  GPSPrefs gps;

  class PowerPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("adc_mult", _parent->adc_multiplier);
      def("pwr_sav_en", _parent->powersaving_enabled);
    }
  public:
    PowerPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  PowerPrefs power;

  class RepeatPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("disable", _parent->disable_fwd);
      def("f_max", _parent->flood_max);
      def("f_max_uns", _parent->flood_max_unscoped);
      def("f_max_adv", _parent->flood_max_advert);
      def("loop", _parent->loop_detect);
    }
  public:
    RepeatPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RepeatPrefs repeat;

  class RoomPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("rd_only", _parent->allow_read_only);
    }
  public:
    RoomPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RoomPrefs room;

protected:
  void structure() override {
    def("name", node_name, sizeof(node_name));
    def("pass", password, sizeof(password));
    def("guest", guest_password, sizeof(guest_password));
    def("owner", owner_info, sizeof(owner_info));
    def("adv_int", advert_interval);
    def("f_adv_int", flood_advert_interval);
    def("lat", node_lat);
    def("lon", node_lon);
    def("disc_mod", discovery_mod_timestamp);  // gates 'since'-filtered DISCOVER replies
    def("radio", radio);
    def("bridge", bridge);
    def("gps", gps);
    def("repeat", repeat);
    def("room", room);
    def("power", power);
  }

public:
  NodePrefs() : ConfigSerializer(), bridge(this), gps(this), radio(this), power(this), repeat(this), room(this) {
    node_name[0] = 0;
    password[0] = 0;
    guest_password[0] = 0;
    bridge_secret[0] = 0;
    owner_info[0] = 0;
  }
};

#ifdef WITH_MQTT_BRIDGE
#include <helpers/MQTTPrefsStorage.h>
static_assert(MQTT_PREFS_SLOT_COUNT == MAX_MQTT_SLOTS,
              "MQTT prefs layout and slot count must change together");

// Observer settings captured from the trailing block of an old-format /com_prefs
// (fork firmware that predates the NodePrefs -> MQTTPrefs split). loadPrefsInt()
// fills this in when it detects the old file layout; loadMQTTPrefs() then applies
// the values one-time if the loaded /mqtt_prefs predates the appended observer
// fields, so SNMP/watchdog/alert config survives the firmware upgrade.
struct LegacyObserverTail {
  bool valid = false;
  uint8_t snmp_enabled;
  char snmp_community[24];
  uint8_t radio_watchdog_minutes;
  uint8_t alert_enabled;
  char alert_psk_hex[33];
  uint16_t alert_wifi_minutes;
  uint16_t alert_mqtt_minutes;
  uint16_t alert_min_interval_min;
  char alert_hashtag[24];
  char alert_region[31];
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
  virtual void setTxPower(int8_t power_dbm) = 0;
  virtual void formatNeighborsReply(char *reply) = 0;
  virtual void removeNeighbor(const uint8_t* pubkey, int key_len) {
    // no op by default
  };
  virtual void formatStatsReply(char *reply) = 0;
  virtual void formatRadioStatsReply(char *reply) = 0;
  virtual void formatRadioDiagReply(char *reply) { strcpy(reply, "Not supported"); }
  virtual void formatPacketStatsReply(char *reply) = 0;
  virtual mesh::LocalIdentity& getSelfId() = 0;
  virtual void saveIdentity(const mesh::LocalIdentity& new_id) = 0;
  virtual void clearStats() = 0;
  virtual void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) = 0;

  virtual void startRegionsLoad() {
    // no op by default
  }
  virtual bool saveRegions() {
    return false;
  }
  virtual void onDefaultRegionChanged(const RegionEntry* r) {
    // no op by default
  }

  virtual void setBridgeState(bool enable) {
    // no op by default
  };

  virtual void restartBridge() {
    // no op by default
  };

  virtual void restartBridgeSlot(int slot) {
    // Default: fall back to full restart
    restartBridge();
  };

  // Schedule a pull-OTA firmware update to run shortly (from the app loop), after
  // the "Beginning update..." CLI reply has been transmitted. Deferred because the
  // flash blocks the loop and then reboots, so it can't run inline with the reply.
  // Returns true if scheduled. Default: not supported.
  virtual bool beginDeferredOtaUpdate() {
    return false;
  };

  virtual int getQueueSize() {
    return 0; // no op by default
  };

  virtual bool syncMqttNtp() {
    return false; // WITH_MQTT_BRIDGE builds override
  };

  virtual bool isMqttBridgeRunning() {
    return false;
  };

  // Browser-based config portal (ESP32 WITH_MQTT_BRIDGE builds override).
  // force_ap=true requests the SoftAP setup portal even when WiFi is configured.
  // Returns true if handled (reply filled either way when true).
  virtual bool startWebConfig(bool force_ap, char* reply) {
    (void)force_ap; (void)reply;
    return false;
  };
  virtual bool stopWebConfig(char* reply) {
    (void)reply;
    return false;
  };

  // Probe all configured NTP servers for connectivity (verbose=serial console gets a
  // detailed table; otherwise reply gets a compact "<server> ok|fail" list).
  virtual bool runMqttNtpDiag(char* reply, size_t reply_size, bool verbose) {
    return false; // WITH_MQTT_BRIDGE builds override
  };

  virtual bool setRxBoostedGain(bool enable) {
    return false; // CommonCLI reports unsupported if not overridden by wrapper
  };

  #if defined(USE_LR2021)
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) {
    return false; // Override in wrapper
  }
  #endif

  // Fault-alert channel hooks (see NodePrefs::alert_*). The default no-op
  // implementations keep CLI commands harmless on builds that don't wire up
  // an AlertReporter.
  virtual void onAlertConfigChanged() {
    // no op by default
  }
  virtual bool sendAlertText(const char* /*text*/) {
    return false; // no op by default
  }
  // Resolve the TransportKey scope to use for outgoing fault-alert floods.
  // Implementations should consult NodePrefs::alert_region first (look up via
  // RegionMap), then fall back to the repeater's default_scope, then return
  // false if neither yields a usable key. AlertReporter falls back to an
  // unscoped flood when this returns false.
  virtual bool resolveAlertScope(TransportKey& /*dest*/) {
    return false; // no op by default
  }
};

#ifdef WITH_MQTT_BRIDGE
namespace MQTTPrefsAtomicStore {
class LegacyUpgradeGate;
}
#endif

class CommonCLI {
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  CommonCLICallbacks* _callbacks;
  mesh::MainBoard* _board;
  SensorManager* _sensors;
  RegionMap* _region_map;
  ClientACL* _acl;
  char tmp[PRV_KEY_SIZE*2 + 4];
#ifdef WITH_MQTT_BRIDGE
  MQTTPrefs _mqtt_prefs;
  LegacyObserverTail _legacy_tail;
  // /mqtt_prefs is newer, corrupt, or temporarily unreadable. The in-memory prefs
  // run on defaults and saveMQTTPrefs() must not overwrite the source file.
  bool _mqtt_prefs_hold = false;
#endif
  bool _com_prefs_needs_upgrade = false;  // old-format legacy prefs detected; rewrite once after load

  mesh::RTCClock* getRTCClock() { return _rtc; }
  void savePrefs();
  void loadPrefsInt(FILESYSTEM* _fs, const char* filename);
#ifdef WITH_MQTT_BRIDGE
  void loadMQTTPrefs(FILESYSTEM* fs, MQTTPrefsAtomicStore::LegacyUpgradeGate* legacy_upgrade);
  bool saveMQTTPrefs(FILESYSTEM* fs);
#endif

  void handleRegionCmd(char* command, char* reply);
  void handleGetCmd(uint32_t sender_timestamp, char* command, char* reply);
  void handleSetCmd(uint32_t sender_timestamp, char* command, char* reply);

  // Observer/MQTT/WiFi/timezone/alert/SNMP CLI handling lives in the fork-owned
  // CommonCLI_Observer.cpp to keep these branches out of the upstream-tracked
  // CommonCLI.cpp. Each returns true if it recognized (handled) the command, or
  // false to fall through to the base get/set parsing.
  bool handleObserverSetCmd(uint32_t sender_timestamp, const char* config, char* reply);
  bool handleObserverGetCmd(uint32_t sender_timestamp, const char* config, char* reply);
  // Observer-only top-level commands (ota check/update, tls.bundletest, alert test)
  // also live in CommonCLI_Observer.cpp; returns true if it handled the command.
  bool handleObserverCommand(uint32_t sender_timestamp, char* command, char* reply);

public:
  CommonCLI(mesh::MainBoard& board, mesh::RTCClock& rtc, SensorManager& sensors, RegionMap& region_map, ClientACL& acl, NodePrefs* prefs, CommonCLICallbacks* callbacks)
      : _board(&board), _rtc(&rtc), _sensors(&sensors), _region_map(&region_map), _acl(&acl), _prefs(prefs), _callbacks(callbacks) { }

  void loadPrefs(FILESYSTEM* _fs);
  bool savePrefs(FILESYSTEM* _fs, bool save_mqtt = true);
  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  mesh::MainBoard* getBoard() { return _board; }
  uint8_t buildAdvertData(uint8_t node_type, uint8_t* app_data);
#ifdef WITH_MQTT_BRIDGE
  // Observer config (MQTT/WiFi/timezone/SNMP/alert), persisted to /mqtt_prefs.
  // Exposed so the app can hand it to MQTTBridge/AlertReporter, which read these
  // fields directly (they no longer live in NodePrefs).
  MQTTPrefs* getObserverPrefs() const { return const_cast<MQTTPrefs*>(&_mqtt_prefs); }
#endif
};
