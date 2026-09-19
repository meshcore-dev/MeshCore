#pragma once

#include "Mesh.h"
#include <helpers/IdentityStore.h>
#include <helpers/SensorManager.h>
#include <helpers/ClientACL.h>
#include <helpers/RegionMap.h>
#include <helpers/ConfigSerializer.h>
#include <helpers/CommonRadioPrefs.h>
#include <helpers/DynamicConfigSerializer.h>

#if defined(WITH_RS232_BRIDGE) || defined(WITH_ESPNOW_BRIDGE)
#define WITH_BRIDGE
#endif

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1
#define ADVERT_LOC_PREFS      2

#define LOOP_DETECT_OFF       0
#define LOOP_DETECT_MINIMAL   1
#define LOOP_DETECT_MODERATE  2
#define LOOP_DETECT_STRICT    3

// Public-key prefix filters (blacklist / whitelist). Entries are FIXED 4-byte pubkey
// prefixes -- the same 8-hex-char prefix the `neighbors`/`clients` CLI displays.
// MAX_KEY_FILTERS is a hard cap: the persisted hex blob (15*4*2 = 120 chars) must
// stay under ConfigSerializer's quoted-token limit (CONFIG_MAX_TOKEN_LEN-1 = 127).
#define MAX_KEY_FILTERS 15

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
  uint32_t guard;
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
  uint8_t bridge_pkt_src = 0; // 0 = logTx, 1 = logRx (default logTx)
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
  uint8_t radio_fem_rxgain = 0; // LoRa FEM RX gain setting
  uint8_t radio_fem_txgain = 0; // LoRa FEM TX gain setting
  uint8_t path_hash_mode = 0;   // which path mode to use when sending
  uint8_t loop_detect = 0;
  uint8_t cad_enabled = 0;      // hardware Channel Activity Detection before TX (boolean)
  uint8_t extra_sf[4];
  // Redundancy-aware FLOOD suppression (simple_repeater). One master switch + SNR/delay params.
  // The threshold C is derived from the neighbour table (adaptive) with a static fallback
  // when no neighbour data is available; it is not user-configurable.
  uint8_t flood_suppress = 0;          // master switch (0=off, 1=on); feature default applied in MyMesh ctor
  int8_t  flood_suppress_snr_hi = 0;   // dB: overheard forward with SNR>=this counts double (central/redundant)
  int8_t  flood_suppress_snr_lo = 0;   // dB: overheard forward with SNR<this counts 0 (preserve edge)
  uint8_t flood_suppress_delay_x = 0;  // extra TX-delay multiplier for central flood relays
  // SNR-repeat fallback is fixed ON (not configurable).

  // Public-key prefix filters. Blacklist: drop ADVERT/ANON_REQ from matching senders
  // at receive (only those types carry the full sender pubkey in clear; data packets
  // expose just 1-byte hashes). Whitelist: repeater never flood-suppresses traffic
  // to/from a matching key, even if the node never checked in. Entries are 4-byte
  // pubkey prefixes; blacklist takes precedence over whitelist.
  uint8_t blacklist_keys[MAX_KEY_FILTERS][4] = {};
  uint8_t blacklist_count = 0;
  uint8_t whitelist_keys[MAX_KEY_FILTERS][4] = {};
  uint8_t whitelist_count = 0;

  // Match a 4-byte pubkey prefix (counts clamped defensively against corrupt prefs).
  bool keyInBlacklist(const uint8_t* key4) const {
    uint8_t n = blacklist_count > MAX_KEY_FILTERS ? MAX_KEY_FILTERS : blacklist_count;
    for (int i = 0; i < n; i++) if (memcmp(blacklist_keys[i], key4, 4) == 0) return true;
    return false;
  }
  bool keyInWhitelist(const uint8_t* key4) const {
    uint8_t n = whitelist_count > MAX_KEY_FILTERS ? MAX_KEY_FILTERS : whitelist_count;
    for (int i = 0; i < n; i++) if (memcmp(whitelist_keys[i], key4, 4) == 0) return true;
    return false;
  }
  // 1-byte hash match (first byte of a whitelisted prefix == dest/src hash of an
  // addressed packet). A false positive (1/256) only causes extra forwarding.
  bool whitelistHash1Match(uint8_t hash1) const {
    uint8_t n = whitelist_count > MAX_KEY_FILTERS ? MAX_KEY_FILTERS : whitelist_count;
    for (int i = 0; i < n; i++) if (whitelist_keys[i][0] == hash1) return true;
    return false;
  }

private:
  class RadioPrefs : public CommonRadioPrefs {
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
    // CommonRadioPrefs interface
    float getFreq() const override { return _parent->freq; }
    void setFreq(float f) override { _parent->freq = f; markDirty(); }
    float getBandwidth() const override { return _parent->bw; }
    void setBandwidth(float bw) override { _parent->bw = bw; markDirty(); }
    uint8_t getSpreadFactor() const override { return _parent->sf; }
    void setSpreadFactor(uint8_t sf) override { _parent->sf = sf; markDirty(); }
    uint8_t getCodingRate() const override { return _parent->cr; }
    void setCodingRate(uint8_t cr) override { _parent->cr = cr; markDirty(); }
    float getAirtimeFactor() const override { return _parent->airtime_factor; }
    void setAirtimeFactor(float af) override { _parent->airtime_factor = af; markDirty(); }
    bool isCadEnabled() const override { return _parent->cad_enabled; }
    void setCadEnabled(bool en) override { _parent->cad_enabled = en; markDirty(); }
    uint8_t getIntThresh() const override { return _parent->interference_threshold; }
    void setIntThresh(uint8_t t) override { _parent->interference_threshold = t; markDirty(); }
    uint8_t getRxGain() const override { return _parent->rx_boosted_gain; }
    void setRxGain(uint8_t g) override { _parent->rx_boosted_gain = g; markDirty(); }
    uint8_t getTxPower() const override { return _parent->tx_power_dbm; }
    void setTxPower(uint8_t dbm) override { _parent->tx_power_dbm = dbm; markDirty(); }
    float getRxDelay() const override { return _parent->rx_delay_base; }
    void setRxDelay(float d) override { _parent->rx_delay_base = d; markDirty(); }
    uint8_t getAgcResetInt() const override { return _parent->agc_reset_interval * 4; }
    void setAgcResetInt(uint8_t secs) override { _parent->agc_reset_interval = secs / 4; markDirty(); }
    uint8_t getHashMode() const override { return _parent->path_hash_mode; }
    void setHashMode(uint8_t m) override { _parent->path_hash_mode = m; markDirty(); }
    uint8_t getMultiAcks() const override { return _parent->multi_acks; }
    void setMultiAcks(uint8_t m) override { _parent->multi_acks = m; markDirty(); }
    float getFloodTxDelay() const override { return _parent->tx_delay_factor; }
    void setFloodTxDelay(float d) override { _parent->tx_delay_factor = d; markDirty(); }
    float getDirectTxDelay() const override { return _parent->direct_tx_delay_factor; }
    void setDirectTxDelay(float d) override { _parent->direct_tx_delay_factor = d; markDirty(); }
    uint8_t getFEMRxGain() const override { return _parent->radio_fem_rxgain; }
    void setFEMRxGain(uint8_t g) override { _parent->radio_fem_rxgain = g; markDirty(); }
    uint8_t getFEMTxGain() const override { return _parent->radio_fem_txgain; }
    void setFEMTxGain(uint8_t g) override { _parent->radio_fem_txgain = g; markDirty(); }
  };
  RadioPrefs radio;

  class BridgePrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->bridge_enabled); // boolean
      def("delay", _parent->bridge_delay);  // milliseconds (default 500 ms)
      def("src", _parent->bridge_pkt_src); // 0 = logTx, 1 = logRx (default logTx)
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
      def("fs", _parent->flood_suppress);
      def("fs_hi", _parent->flood_suppress_snr_hi);
      def("fs_lo", _parent->flood_suppress_snr_lo);
      def("fs_dx", _parent->flood_suppress_delay_x);
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

  DynamicConfigSerializer custom;

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
    def("radio", radio);
    def("bridge", bridge);
    def("gps", gps);
    def("repeat", repeat);
    def("room", room);
    def("power", power);
    def("blacklist", blacklist_keys, sizeof(blacklist_keys));   // binary blob -> quoted hex
    def("blacklist_n", blacklist_count);
    def("whitelist", whitelist_keys, sizeof(whitelist_keys));   // binary blob -> quoted hex
    def("whitelist_n", whitelist_count);
    def("custom", custom);
  }

public:
  NodePrefs() : ConfigSerializer(), bridge(this), gps(this), radio(this), power(this), repeat(this), room(this), custom(&radio) {
    node_name[0] = 0;
    password[0] = 0;
    guest_password[0] = 0;
    bridge_secret[0] = 0;
    owner_info[0] = 0;
  }

  CommonRadioPrefs* getRadioPrefs() { return &radio; }
  KeyValueStore* getCustom() { return &custom; }

  bool isDirty() const override { return ConfigSerializer::isDirty() || radio.isDirty() || custom.isDirty(); }
  void clearDirty() override { ConfigSerializer::clearDirty(); radio.clearDirty(); custom.clearDirty(); }
};

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
  // A blacklist entry was just added (key4 = 4-byte pubkey prefix). Roles with learned
  // per-node state (neighbour / attached-client tables) should purge matching entries.
  virtual void onBlacklistEntryAdded(const uint8_t* key4) {
    // no op by default
  };
  virtual void formatStatsReply(char *reply) = 0;
  virtual void formatRadioStatsReply(char *reply) = 0;
  virtual void formatPacketStatsReply(char *reply) = 0;
  // Appends the suppression-ratio suffix (", suppressed N/M (P%)") to the flood.suppress get-reply.
  // Default is a no-op so non-repeater roles keep the plain "> on/off" reply.
  virtual void formatFloodSuppressRatioReply(char *reply) { }
  // List directly-attached leaf clients (client-aware suppression). Default no-op.
  virtual void formatClientsReply(char *reply) { }
  // Reach edges of one near repeater (directed inter-neighbour graph), looked up by
  // a hash prefix. Default no-op. hash_len is the number of prefix bytes parsed.
  virtual void formatReachReply(char *reply, const uint8_t* hash, uint8_t hash_len) { }
  // Near coverage peers (fresh + SNR>=snr_lo), strongest first, with the active
  // snr_lo threshold and the coverage cap. Default no-op.
  virtual void formatNearReply(char *reply) { }
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

  virtual bool setRxBoostedGain(bool enable) {
    return false; // CommonCLI reports unsupported if not overridden by wrapper
  };

  #if defined(USE_LR2021)
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) {
    return false; // Override in wrapper
  } 
  #endif
};

class CommonCLI {
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  CommonCLICallbacks* _callbacks;
  mesh::MainBoard* _board;
  SensorManager* _sensors;
  RegionMap* _region_map;
  ClientACL* _acl;
  char tmp[PRV_KEY_SIZE*2 + 4];

  mesh::RTCClock* getRTCClock() { return _rtc; }
  void savePrefs();
  void loadPrefsInt(FILESYSTEM* _fs, const char* filename);

  void handleRegionCmd(char* command, char* reply);
  void handleKeyFilterCmd(uint8_t keys[][4], uint8_t* count, const char* args, bool is_blacklist, char* reply);
  void handleGetCmd(uint32_t sender_timestamp, char* command, char* reply);
  void handleSetCmd(uint32_t sender_timestamp, char* command, char* reply);

public:
  CommonCLI(mesh::MainBoard& board, mesh::RTCClock& rtc, SensorManager& sensors, RegionMap& region_map, ClientACL& acl, NodePrefs* prefs, CommonCLICallbacks* callbacks)
      : _board(&board), _rtc(&rtc), _sensors(&sensors), _region_map(&region_map), _acl(&acl), _prefs(prefs), _callbacks(callbacks) { }

  void loadPrefs(FILESYSTEM* _fs);
  bool savePrefs(FILESYSTEM* _fs);
  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  uint8_t buildAdvertData(uint8_t node_type, uint8_t* app_data);
};
