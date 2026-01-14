#include <Arduino.h>
#include <Mesh.h>

#if defined(NRF52_PLATFORM)
  #include <InternalFileSystem.h>
  #define FILESYSTEM InternalFS
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  #define FILESYSTEM LittleFS
#elif defined(ESP32)
  #include <SPIFFS.h>
  #include <esp_system.h>
  #define FILESYSTEM SPIFFS
#else
  #error "Unsupported platform"
#endif

#include <ArduinoJson.h>
#include <Utils.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <target.h>
#include <RTClib.h>

#include <cstring>
#include <vector>

#ifndef LORA_FREQ
  #define LORA_FREQ   915.0
#endif
#ifndef LORA_BW
  #define LORA_BW     250
#endif
#ifndef LORA_SF
  #define LORA_SF     10
#endif
#ifndef LORA_CR
  #define LORA_CR      5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  20
#endif

namespace {
constexpr const char* kConfigPath = "/usb_chatbot.json";
constexpr size_t kSerialBufSize = 512;
constexpr size_t kJsonDocSize = 512;
constexpr uint32_t kSendBaseTimeoutMs = 500;
constexpr float kFloodTimeoutFactor = 16.0f;
constexpr float kDirectTimeoutFactor = 6.0f;
constexpr uint32_t kDirectTimeoutExtra = 250;
}  // namespace

struct UsbChatPrefs {
  char node_name[32];
  char channel_name[32];
  char channel_key_hex[65];  // up to 32 bytes => 64 hex chars + null
};

class UsbChatManager;

class UsbChatMesh : public BaseChatMesh {
public:
  UsbChatMesh(mesh::Radio& radio, StdRNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
    : BaseChatMesh(radio,
                   *(_clock = new ArduinoMillis()),
                   rng,
                   rtc,
                   *(_packetManager = new StaticPoolPacketManager(8)),
                   tables),
      _manager(nullptr),
      _channel(nullptr) {}

  void begin(FILESYSTEM& fs, UsbChatManager* manager, UsbChatPrefs& prefs);
  bool configureChannel(const char* name, const char* key_hex);
  bool sendChannelMessage(const char* sender_name, const char* text);
  bool isChannelReady() const { return _channel != nullptr; }

  void setManager(UsbChatManager* manager) { _manager = manager; }

protected:
  // BaseChatMesh overrides we keep minimal for the USB bridge use-case
  void onDiscoveredContact(ContactInfo& /*contact*/, bool /*is_new*/, uint8_t /*path_len*/, const uint8_t* /*path*/) override {}
  ContactInfo* processAck(const uint8_t* /*data*/) override { return nullptr; }
  void onContactPathUpdated(const ContactInfo& /*contact*/) override {}
  void onMessageRecv(const ContactInfo& /*contact*/, mesh::Packet* /*pkt*/, uint32_t /*sender_timestamp*/, const char* /*text*/) override {}
  void onCommandDataRecv(const ContactInfo& /*contact*/, mesh::Packet* /*pkt*/, uint32_t /*sender_timestamp*/, const char* /*text*/) override {}
  void onSignedMessageRecv(const ContactInfo& /*contact*/, mesh::Packet* /*pkt*/, uint32_t /*sender_timestamp*/, const uint8_t* /*sender_prefix*/, const char* /*text*/) override {}
  void onContactResponse(const ContactInfo& /*contact*/, const uint8_t* /*data*/, uint8_t /*len*/) override {}
  uint8_t onContactRequest(const ContactInfo& /*contact*/, uint32_t /*sender_timestamp*/, const uint8_t* /*data*/, uint8_t /*len*/, uint8_t* /*reply*/) override { return 0; }

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override {
    return kSendBaseTimeoutMs + static_cast<uint32_t>(kFloodTimeoutFactor * pkt_airtime_millis);
  }

  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override {
    return kSendBaseTimeoutMs +
           static_cast<uint32_t>((pkt_airtime_millis * kDirectTimeoutFactor + kDirectTimeoutExtra) * (path_len + 1));
  }

  void onSendTimeout() override;

  void onChannelMessageRecv(const mesh::GroupChannel& /*channel*/, mesh::Packet* pkt, uint32_t timestamp, const char* text) override;

  bool isAutoAddEnabled() const override { return false; }

private:
  UsbChatManager* _manager;
  ChannelDetails* _channel;
  ArduinoMillis* _clock;
  StaticPoolPacketManager* _packetManager;

  static bool isYear(uint32_t ts, int year) {
    DateTime dt(ts);
    return dt.year() == year;
  }

  static void copyString(char* dest, size_t dest_size, const char* src) {
    if (dest_size == 0) return;
    if (src == nullptr) {
      dest[0] = 0;
      return;
    }
    size_t len = strlen(src);
    if (len >= dest_size) len = dest_size - 1;
    memcpy(dest, src, len);
    dest[len] = 0;
  }

  static String toBase64(const uint8_t* data, size_t len) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String encoded;
    encoded.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
      uint32_t chunk = static_cast<uint32_t>(data[i]) << 16;
      size_t remaining = len - i;
      if (remaining > 1) chunk |= static_cast<uint32_t>(data[i + 1]) << 8;
      if (remaining > 2) chunk |= static_cast<uint32_t>(data[i + 2]);
      encoded += alphabet[(chunk >> 18) & 0x3F];
      encoded += alphabet[(chunk >> 12) & 0x3F];
      encoded += (remaining > 1) ? alphabet[(chunk >> 6) & 0x3F] : '=';
      encoded += (remaining > 2) ? alphabet[chunk & 0x3F] : '=';
    }
    return encoded;
  }

  bool applyChannelSecret(const std::vector<uint8_t>& key_bytes) {
    if (!_channel) return false;
    if (key_bytes.empty() || key_bytes.size() > sizeof(_channel->channel.secret)) return false;

    memset(_channel->channel.secret, 0, sizeof(_channel->channel.secret));
    memcpy(_channel->channel.secret, key_bytes.data(), key_bytes.size());
    mesh::Utils::sha256(_channel->channel.hash,
                        sizeof(_channel->channel.hash),
                        _channel->channel.secret,
                        key_bytes.size());
    return true;
  }
};

class UsbChatManager {
public:
  UsbChatManager(UsbChatMesh& mesh, mesh::RTCClock& rtc)
    : _mesh(mesh),
      _rtc(rtc),
      _fs(nullptr),
      _serial_len(0) {}

  void begin(FILESYSTEM& fs) {
    _fs = &fs;
    loadPrefs();
    _mesh.setManager(this);
    _mesh.begin(fs, this, _prefs);
    if (_prefs.channel_key_hex[0] != 0) {
      _mesh.configureChannel(_prefs.channel_name, _prefs.channel_key_hex);
    }
    publishConfig();
  }

  void loop() {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r') continue;
      if (c == '\n') {
        _serial_buf[_serial_len] = 0;
        handleLine(_serial_buf);
        _serial_len = 0;
      } else if (_serial_len < kSerialBufSize - 1) {
        _serial_buf[_serial_len++] = c;
      } else {
        _serial_len = 0;  // reset on overflow
      }
    }
  }

  void handleMeshMessage(uint32_t timestamp, const char* text, bool direct_route) {
    const char* colon = strstr(text, ": ");
    const char* sender = "unknown";
    const char* body = text;
    char sender_buf[48];
    if (colon) {
      size_t name_len = static_cast<size_t>(colon - text);
      if (name_len >= sizeof(sender_buf)) name_len = sizeof(sender_buf) - 1;
      memcpy(sender_buf, text, name_len);
      sender_buf[name_len] = 0;
      sender = sender_buf;
      body = colon + 2;
    }

    StaticJsonDocument<kJsonDocSize> doc;
    doc["event"] = "rx";
    doc["text"] = body;
    doc["sender"] = sender;
    doc["timestamp"] = timestamp;
    doc["direct"] = direct_route;
    emit(doc);
  }

  void notifySendTimeout() {
    StaticJsonDocument<128> doc;
    doc["event"] = "tx";
    doc["ok"] = false;
    doc["error"] = "timeout";
    emit(doc);
  }

  void publishConfig() {
    StaticJsonDocument<kJsonDocSize> doc;
    doc["event"] = "config";
    doc["node_name"] = _prefs.node_name;
    doc["channel_name"] = _prefs.channel_name;
    doc["channel_key_hex"] = _prefs.channel_key_hex;
    doc["channel_ready"] = _mesh.isChannelReady();
    doc["rtc"] = _rtc.getCurrentTime();
    char pub_hex[PUB_KEY_SIZE * 2 + 1];
    mesh::Utils::toHex(pub_hex, _mesh.self_id.pub_key, PUB_KEY_SIZE);
    doc["identity_pub"] = pub_hex;
    emit(doc);
  }

  const UsbChatPrefs& prefs() const { return _prefs; }

private:
  UsbChatMesh& _mesh;
  mesh::RTCClock& _rtc;
  FILESYSTEM* _fs;
  UsbChatPrefs _prefs{};
  char _serial_buf[kSerialBufSize];
  size_t _serial_len;

  static void copyString(char* dest, size_t dest_size, const char* src) {
    if (dest_size == 0) return;
    if (!src) {
      dest[0] = 0;
      return;
    }
    size_t len = strlen(src);
    if (len >= dest_size) len = dest_size - 1;
    memcpy(dest, src, len);
    dest[len] = 0;
  }

  void emit(const JsonDocument& doc) {
    serializeJson(doc, Serial);
    Serial.println();
  }

  void sendOk(const char* detail = nullptr) {
    StaticJsonDocument<128> doc;
    doc["event"] = "ok";
    if (detail) doc["detail"] = detail;
    emit(doc);
  }

  void sendError(const char* message) {
    StaticJsonDocument<128> doc;
    doc["event"] = "error";
    doc["message"] = message;
    emit(doc);
  }

  void handleLine(const char* line) {
    StaticJsonDocument<kJsonDocSize> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
      sendError("bad_json");
      return;
    }

    const char* cmd = doc["cmd"] | "";
    if (strcmp(cmd, "get_config") == 0) {
      publishConfig();
    } else if (strcmp(cmd, "set_config") == 0) {
      const char* name = doc["node_name"] | _prefs.node_name;
      const char* channel_name = doc["channel_name"] | _prefs.channel_name;
      const char* key_hex = doc["channel_key_hex"] | _prefs.channel_key_hex;

      copyString(_prefs.node_name, sizeof(_prefs.node_name), name);
      copyString(_prefs.channel_name, sizeof(_prefs.channel_name), channel_name);
      copyString(_prefs.channel_key_hex, sizeof(_prefs.channel_key_hex), key_hex);

      if (_prefs.channel_key_hex[0] == 0) {
        sendError("channel_key_required");
        return;
      }

      if (!_mesh.configureChannel(_prefs.channel_name, _prefs.channel_key_hex)) {
        sendError("bad_channel_key");
        return;
      }

      savePrefs();
      sendOk("config_saved");
      publishConfig();
    } else if (strcmp(cmd, "send") == 0) {
      const char* text = doc["text"] | "";
      if (!_mesh.isChannelReady()) {
        sendError("channel_not_ready");
        return;
      }
      if (strlen(text) == 0) {
        sendError("empty_text");
        return;
      }
      bool ok = _mesh.sendChannelMessage(_prefs.node_name, text);
      StaticJsonDocument<128> out;
      out["event"] = "tx";
      out["ok"] = ok;
      if (!ok) out["error"] = "send_failed";
      emit(out);
    } else if (strcmp(cmd, "set_time") == 0) {
      uint32_t ts = doc["timestamp"] | 0;
      _rtc.setCurrentTime(ts);
      sendOk("time_set");
    } else if (strcmp(cmd, "reboot") == 0) {
      sendOk("rebooting");
#if defined(ESP32)
      delay(50);
      esp_restart();
#else
      sendError("reboot_unsupported_platform");
#endif
    } else {
      sendError("unknown_cmd");
    }
  }

  bool loadPrefs() {
    // defaults
    copyString(_prefs.node_name, sizeof(_prefs.node_name), "USBChat");
    copyString(_prefs.channel_name, sizeof(_prefs.channel_name), "USB-Channel");
    _prefs.channel_key_hex[0] = 0;

    if (_fs == nullptr || !_fs->exists(kConfigPath)) return false;

    File f = _fs->open(kConfigPath, "r");
    if (!f) return false;

    StaticJsonDocument<kJsonDocSize> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    copyString(_prefs.node_name, sizeof(_prefs.node_name), doc["node_name"] | _prefs.node_name);
    copyString(_prefs.channel_name, sizeof(_prefs.channel_name), doc["channel_name"] | _prefs.channel_name);
    copyString(_prefs.channel_key_hex, sizeof(_prefs.channel_key_hex), doc["channel_key_hex"] | "");
    return true;
  }

  void savePrefs() {
    if (_fs == nullptr) return;

    StaticJsonDocument<kJsonDocSize> doc;
    doc["node_name"] = _prefs.node_name;
    doc["channel_name"] = _prefs.channel_name;
    doc["channel_key_hex"] = _prefs.channel_key_hex;

    File f = _fs->open(kConfigPath, "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
  }
};

void UsbChatMesh::begin(FILESYSTEM& fs, UsbChatManager* manager, UsbChatPrefs& prefs) {
  setManager(manager);
  BaseChatMesh::begin();

  IdentityStore store(fs, "/identity");
#if defined(RP2040_PLATFORM)
  store.begin();
#endif
  if (!store.load("_main", self_id, prefs.node_name, sizeof(prefs.node_name))) {
    self_id = mesh::LocalIdentity(getRNG());
    int attempts = 0;
    while (attempts < 5 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {
      self_id = mesh::LocalIdentity(getRNG());
      attempts++;
    }
    store.save("_main", self_id);
  }
}

bool UsbChatMesh::configureChannel(const char* name, const char* key_hex) {
  if (!key_hex || !name) return false;
  size_t hex_len = strlen(key_hex);
  if (hex_len != 32 && hex_len != 64) return false;  // 16 or 32 bytes, hex encoded

  std::vector<uint8_t> key_bytes(hex_len / 2);
  if (!mesh::Utils::fromHex(key_bytes.data(), key_bytes.size(), key_hex)) return false;

  if (!_channel) {
    String b64 = toBase64(key_bytes.data(), key_bytes.size());
    _channel = addChannel(name, b64.c_str());
    if (!_channel) return false;
  }

  applyChannelSecret(key_bytes);
  copyString(_channel->name, sizeof(_channel->name), name);
  return true;
}

bool UsbChatMesh::sendChannelMessage(const char* sender_name, const char* text) {
  if (!_channel) return false;
  String clean_text = text;
  clean_text.trim();
  if (clean_text.length() == 0) return false;

  String sender = sender_name ? sender_name : "USBChat";
  sender.trim();
  if (sender.length() == 0) sender = "USBChat";

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  return sendGroupMessage(now, _channel->channel, sender.c_str(), clean_text.c_str(), clean_text.length());
}

void UsbChatMesh::onSendTimeout() {
  if (_manager) _manager->notifySendTimeout();
}

void UsbChatMesh::onChannelMessageRecv(const mesh::GroupChannel& /*channel*/, mesh::Packet* pkt, uint32_t timestamp, const char* text) {
  uint32_t now = getRTCClock()->getCurrentTime();
  if (isYear(now, 2024) && timestamp > 1704067200UL && timestamp < 4102444800UL) {
    getRTCClock()->setCurrentTime(timestamp);
  }

  if (_manager) {
    bool direct = pkt ? pkt->isRouteDirect() : false;
    _manager->handleMeshMessage(timestamp, text, direct);
  }
}

StdRNG fast_rng;
SimpleMeshTables tables;
UsbChatMesh usb_mesh(radio_driver, fast_rng, rtc_clock, tables);
UsbChatManager manager(usb_mesh, rtc_clock);

void halt() {
  while (true) {}
}

void setup() {
  Serial.begin(115200);
  board.begin();

  if (!radio_init()) {
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  manager.begin(InternalFS);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  manager.begin(LittleFS);
#elif defined(ESP32)
  SPIFFS.begin(true);
  manager.begin(SPIFFS);
#endif

  radio_set_params(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR);
  radio_set_tx_power(LORA_TX_POWER);

  // optional hello advert
  auto pkt = usb_mesh.createSelfAdvert(manager.prefs().node_name);
  if (pkt) usb_mesh.sendZeroHop(pkt);
}

void loop() {
  usb_mesh.loop();
  rtc_clock.tick();
  manager.loop();
}