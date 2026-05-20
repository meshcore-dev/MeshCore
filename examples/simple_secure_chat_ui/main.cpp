#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <time.h>
#include <sys/time.h>

#if defined(NRF52_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/IdentityStore.h>
#include <RTClib.h>
#include <target.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#define FIRMWARE_VER_TEXT   "v2 (build: 4 Feb 2025)"

#ifndef LORA_FREQ
  #define LORA_FREQ   869.525
#endif
#ifndef LORA_BW
  #define LORA_BW     250
#endif
#ifndef LORA_SF
  #define LORA_SF     11
#endif
#ifndef LORA_CR
  #define LORA_CR      5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  20
#endif

#ifndef MAX_CONTACTS
  #define MAX_CONTACTS         100
#endif

#include <helpers/BaseChatMesh.h>

#include "esp_log.h"
#include <lvgl.h>
#include <Wire.h>
#ifndef SEEED_SENSECAP_INDICATOR
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#endif

#include "../src/fonts/fonts.h"
#include "../lvgl/lvgl.h"

#include "../include/uiExternals.h"
#include "../include/lgfx.h"
#include "../include/uiDefines.h"
#include "../include/uiManager.h"
#include "../include/uiTasks.h"
#include "uiTouch.h"
#include "messageStore.h"

#define TAG "main"

#define SEND_TIMEOUT_BASE_MILLIS          500
#define FLOOD_SEND_TIMEOUT_FACTOR         16.0f
#define DIRECT_SEND_PERHOP_FACTOR         6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS   250

#define PUBLIC_GROUP_PSK  "izOH6cXN6mrJ5e26oRXNcg=="

static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf[800 * 480 / 10];
//static lv_color_t disp_draw_buf;
static lv_disp_drv_t disp_drv;

UIManager *uiManager;

SemaphoreHandle_t semaphoreData;

volatile bool g_clock_synced = false;

TwoWire I2Cone = TwoWire(0);
#ifndef SEEED_SENSECAP_INDICATOR
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 64, &I2Cone, OLED_RESET);
#endif

SPIClass& spi = SPI;
uint16_t touchCalibration_x0 = 300, touchCalibration_x1 = 3600, touchCalibration_y0 = 300, touchCalibration_y1 = 3600;
uint8_t  touchCalibration_rotate = 1, touchCalibration_invert_x = 2, touchCalibration_invert_y = 0;

void format_time(uint32_t ts, char *buf, size_t len)
{
    time_t t = ts;
    struct tm *tm_info = localtime(&t);
    strftime(buf, len, "%H:%M:%S", tm_info);
}

void parse_group_message(const char *input,
                         char *sender_out, size_t sender_len,
                         char *msg_out, size_t msg_len)
{
    const char *sep = strchr(input, ':');

    if (!sep) {
        strncpy(sender_out, "Unknown", sender_len);
        strncpy(msg_out, input, msg_len);
        return;
    }

    size_t name_len = sep - input;
    if (name_len >= sender_len) name_len = sender_len - 1;

    strncpy(sender_out, input, name_len);
    sender_out[name_len] = 0;

    // Skip ": "
    const char *msg_start = sep + 1;
    if (*msg_start == ' ') msg_start++;

    strncpy(msg_out, msg_start, msg_len);
}

// Elecrow Display callbacks
/* Display flushing */
static uint32_t s_flush_count = 0;
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  s_flush_count++;
  if (s_flush_count <= 3) {
    Serial.printf("[display] flush #%u  x=%d y=%d w=%d h=%d\n",
                  s_flush_count, area->x1, area->y1, w, h);
  }

#if (LV_COLOR_16_SWAP != 0)
  lcd.pushImageDMA(area->x1, area->y1, w, h,(lgfx::rgb565_t*)&color_p->full);
#else
  lcd.pushImageDMA(area->x1, area->y1, w, h,(lgfx::rgb565_t*)&color_p->full);
#endif

  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data)
{
  // Serialize Wire access with SenseCapHAL (TCA9535 radio expander).
  // g_i2c_mutex is nullptr until radio_init() creates it — safe to skip then.
  // Must check the return value: if we don't hold the mutex, skip getTouch()
  // entirely to avoid racing with TCA9535 I2C transactions and corrupting the bus.
  bool have_mutex = !g_i2c_mutex || (xSemaphoreTake(g_i2c_mutex, portMAX_DELAY) == pdTRUE);

  uint16_t x, y;
  bool touched = have_mutex && lcd.getTouch(&x, &y);

  if (have_mutex && g_i2c_mutex) xSemaphoreGive(g_i2c_mutex);

  if (touched)
  {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
  }
}

// void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
// {
//   if (touch_has_signal())
//   {
//     if (touch_touched())
//     {
//       data->state = LV_INDEV_STATE_PR;

//       /*Set the coordinates*/
//       data->point.x = touch_last_x;
//       data->point.y = touch_last_y;
//       // #ifndef MODE_RELEASE
//       //   Serial.printf("Data x: %d, Data y: %d", touch_last_x, touch_last_y);
//       //   Serial.println();
//       // #endif
//     }
//     else if (touch_released())
//     {
//       data->state = LV_INDEV_STATE_REL;
//     }
//   }
//   else
//   {
//     data->state = LV_INDEV_STATE_REL;
//   }
//   delay(15);
// }

void initializeUI() {  

  Serial.println("initialize UI...");  
  
  /*
  #ifdef ENABLE_STARTUP_LOGO
  lv_disp_load_scr(ui_ScreenLogo);  
  for( int i=0; i < 100; i++ ){
      lv_task_handler();  
      delay(10);
  }
  lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, true);
  #else
  ui_init();
  #endif
  */
  uiManager = new UIManager();
  uiManager->setNightMode(false);  
}

void createSemaphores() {
  semaphoreData = xSemaphoreCreateMutex();
  xSemaphoreGive(semaphoreData);
}

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

/* -------------------------------------------------------------------------------------- */

static void msgstore_dm_path(char* out, size_t len, const uint8_t* pub_key) {
  snprintf(out, len, "/m_%02x%02x%02x%02x.log",
           pub_key[0], pub_key[1], pub_key[2], pub_key[3]);
}

static const char* MSGSTORE_PUBLIC_PATH = "/m_public.log";

static void msgstore_rotate_if_needed(const char* path) {
#if defined(ESP32)
  if (!SPIFFS.exists(path)) return;
  File f = SPIFFS.open(path, "r");
  if (!f) return;
  size_t sz = f.size();
  f.close();
  if (sz > MSGSTORE_MAX_BYTES) SPIFFS.remove(path);
#endif
}

void msgstore_append_dm(const uint8_t* pub_key, uint32_t ts, bool sent, const char* text) {
#if defined(ESP32)
  char path[24];
  msgstore_dm_path(path, sizeof(path), pub_key);
  msgstore_rotate_if_needed(path);
  File f = SPIFFS.open(path, FILE_APPEND);
  if (!f) return;
  f.printf("%u|%c|%s\n", (unsigned)ts, sent ? '>' : '<', text ? text : "");
  f.close();
#endif
}

void msgstore_append_public(uint32_t ts, const char* sender, bool sent, const char* text) {
#if defined(ESP32)
  msgstore_rotate_if_needed(MSGSTORE_PUBLIC_PATH);
  File f = SPIFFS.open(MSGSTORE_PUBLIC_PATH, FILE_APPEND);
  if (!f) return;
  f.printf("%u|%s|%c|%s\n", (unsigned)ts,
           sender ? sender : "?", sent ? '>' : '<', text ? text : "");
  f.close();
#endif
}

// Returns true if a DM file line is valid; fills ts, dir, text.
static bool parse_dm_line(const String& line, uint32_t& ts, char& dir, String& text) {
  int p1 = line.indexOf('|');
  int p2 = line.indexOf('|', p1 + 1);
  if (p1 < 1 || p2 < p1 + 2) return false;
  ts = (uint32_t) line.substring(0, p1).toInt();
  if (ts == 0) return false;
  dir = line.charAt(p1 + 1);
  if (dir != '>' && dir != '<') return false;
  text = line.substring(p2 + 1);
  text.trim();
  return text.length() > 0;
}

void msgstore_load_dm(const uint8_t* pub_key) {
#if defined(ESP32)
  // LVGL heap is 48 KB, shared with all UI widgets. Each bubble needs ~4 LVGL
  // objects (~600 B). Limit history to the last 20 messages to avoid OOM.
  static const int MAX_DISPLAY = 20;

  char path[24];
  msgstore_dm_path(path, sizeof(path), pub_key);
  if (!SPIFFS.exists(path)) return;

  // Pass 1 — count valid lines so we know how many to skip
  File f = SPIFFS.open(path, "r");
  if (!f) return;
  int total = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    uint32_t ts; char dir; String text;
    if (parse_dm_line(line, ts, dir, text)) total++;
  }
  f.close();

  // Pass 2 — skip oldest, render only the last MAX_DISPLAY valid entries
  f = SPIFFS.open(path, "r");
  if (!f) return;
  int skip = (total > MAX_DISPLAY) ? total - MAX_DISPLAY : 0;
  int seen = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    uint32_t ts; char dir; String text;
    if (!parse_dm_line(line, ts, dir, text)) continue;
    if (seen++ < skip) continue;
    char time_buf[16];
    format_time(ts, time_buf, sizeof(time_buf));
    uiManager->addPrivateChatBubble(time_buf, text.c_str(), dir == '>', false);
  }
  f.close();
  // scroll is done by the caller (handleContactClick) after flex layout is re-enabled
#endif
}

void msgstore_load_public() {
#if defined(ESP32)
  static const int MAX_DISPLAY = 20;

  if (!SPIFFS.exists(MSGSTORE_PUBLIC_PATH)) return;

  // Pass 1 — count valid lines
  File f = SPIFFS.open(MSGSTORE_PUBLIC_PATH, "r");
  if (!f) return;
  int total = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    int p3 = line.indexOf('|', p2 + 1);
    if (p1 < 1 || p2 < p1 + 2 || p3 < p2 + 2) continue;
    if ((uint32_t)line.substring(0, p1).toInt() == 0) continue;
    char dir = line.charAt(p2 + 1);
    if (dir != '>' && dir != '<') continue;
    String text = line.substring(p3 + 1); text.trim();
    if (text.length() == 0) continue;
    total++;
  }
  f.close();

  // Pass 2 — skip oldest, render last MAX_DISPLAY
  f = SPIFFS.open(MSGSTORE_PUBLIC_PATH, "r");
  if (!f) return;
  int skip = (total > MAX_DISPLAY) ? total - MAX_DISPLAY : 0;
  int seen = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    int p3 = line.indexOf('|', p2 + 1);
    if (p1 < 1 || p2 < p1 + 2 || p3 < p2 + 2) continue;
    uint32_t ts = (uint32_t)line.substring(0, p1).toInt();
    if (ts == 0) continue;
    String sender = line.substring(p1 + 1, p2);
    char dir = line.charAt(p2 + 1);
    if (dir != '>' && dir != '<') continue;
    String text = line.substring(p3 + 1); text.trim();
    if (text.length() == 0) continue;
    if (seen++ < skip) continue;
    char time_buf[16];
    format_time(ts, time_buf, sizeof(time_buf));
    uiManager->addChatBubble(time_buf, sender.c_str(), text.c_str(), dir == '>', false);
  }
  f.close();
  // scroll done by caller after endPublicHistoryLoad() re-enables flex
#endif
}

/* -------------------------------------------------------------------------------------- */

struct NodePrefs {  // persisted to file
  float airtime_factor;
  char node_name[32];
  double node_lat, node_lon;
  float freq;
  uint8_t tx_power_dbm;
  uint8_t sf;   // was unused[0]
  uint8_t cr;   // was unused[1]
  uint8_t _pad; // was unused[2]
  float bw;     // new field (zeros from old 60-byte files → defaults applied in begin())
};

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "19 Apr 2026"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.15.0"
#endif

#define FIRMWARE_ROLE "Chat"

class MyMesh : public BaseChatMesh, ContactVisitor {
  FILESYSTEM* _fs;
  NodePrefs _prefs;
  uint32_t expected_ack_crc;
  ChannelDetails* _public;
  unsigned long last_msg_sent;
  ContactInfo* curr_recipient;
  char command[512+10];
  uint8_t tmp_buf[256];
  char hex_buf[512];

  const char* getTypeName(uint8_t type) const {
    if (type == ADV_TYPE_CHAT) return "Chat";
    if (type == ADV_TYPE_REPEATER) return "Repeater";
    if (type == ADV_TYPE_ROOM) return "Room";
    return "??";  // unknown
  }

  void loadContacts() {
    if (_fs->exists("/contacts")) {
    #if defined(RP2040_PLATFORM)
      File file = _fs->open("/contacts", "r");
    #else
      File file = _fs->open("/contacts");
    #endif
      if (file) {
        bool full = false;
        while (!full) {
          ContactInfo c;
          uint8_t pub_key[32];
          uint8_t unused;

          bool success = (file.read(pub_key, 32) == 32);
          success = success && (file.read((uint8_t *) &c.name, 32) == 32);
          success = success && (file.read(&c.type, 1) == 1);
          success = success && (file.read(&c.flags, 1) == 1);
          success = success && (file.read(&unused, 1) == 1);
          success = success && (file.read((uint8_t *) &c.lastmod, 4) == 4);
          success = success && (file.read((uint8_t *) &c.out_path_len, 1) == 1);
          success = success && (file.read((uint8_t *) &c.last_advert_timestamp, 4) == 4);
          success = success && (file.read(c.out_path, 64) == 64);
          c.gps_lat = c.gps_lon = 0;   // not yet supported

          if (!success) break;  // EOF

          c.id = mesh::Identity(pub_key);
          uiManager->addContactToUI(c);
          if (!addContact(c)) full = true;
        }
        file.close();
      }
    }
  }

  void saveContacts() {
#if defined(NRF52_PLATFORM)
    _fs->remove("/contacts");
    File file = _fs->open("/contacts", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File file = _fs->open("/contacts", "w");
#else
    File file = _fs->open("/contacts", "w", true);
#endif
    if (file) {
      ContactsIterator iter;
      ContactInfo c;
      uint8_t unused = 0;

      while (iter.hasNext(this, c)) {
        bool success = (file.write(c.id.pub_key, 32) == 32);
        success = success && (file.write((uint8_t *) &c.name, 32) == 32);
        success = success && (file.write(&c.type, 1) == 1);
        success = success && (file.write(&c.flags, 1) == 1);
        success = success && (file.write(&unused, 1) == 1);
        success = success && (file.write((uint8_t *) &c.lastmod, 4) == 4);
        success = success && (file.write((uint8_t *) &c.out_path_len, 1) == 1);
        success = success && (file.write((uint8_t *) &c.last_advert_timestamp, 4) == 4);
        success = success && (file.write(c.out_path, 64) == 64);

        if (!success) break;  // write failed
      }
      file.close();
    }
  }

  void setClock(uint32_t timestamp) {
    getRTCClock()->setCurrentTime(timestamp);
    struct timeval tv = { .tv_sec = (time_t)timestamp, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    g_clock_synced = true;
    Serial.println("   (OK - clock set!)");
    if (uiManager) {
      time_t ts = (time_t)timestamp;
      struct tm t;
      localtime_r(&ts, &t);
      uiManager->updateDateTime(t);
    }
  }

  void importCard(const char* command) {
    while (*command == ' ') command++;   // skip leading spaces
    if (memcmp(command, "meshcore://", 11) == 0) {
      command += 11;  // skip the prefix
      char *ep = strchr(command, 0);  // find end of string
      while (ep > command) {
        ep--;
        if (mesh::Utils::isHexChar(*ep)) break;  // found tail end of card
        *ep = 0;  // remove trailing spaces and other junk
      }
      int len = strlen(command);
      if (len % 2 == 0) {
        len >>= 1;  // halve, for num bytes
        if (mesh::Utils::fromHex(tmp_buf, len, command)) {
          importContact(tmp_buf, len);
          return;
        }
      }
    }
    Serial.println("   error: invalid format");
  }

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  int calcRxDelay(float score, uint32_t air_time) const override {
    return 0;  // disable rxdelay
  }

  bool allowPacketForward(const mesh::Packet* packet) override {
    return true;
  }

  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override {
    Serial.printf("ADVERT from -> %s\n", contact.name);
    Serial.printf("  type: %s\n", getTypeName(contact.type));
    Serial.print("   public key: "); mesh::Utils::printHex(Serial, contact.id.pub_key, PUB_KEY_SIZE); Serial.println();

    if (is_new) {
      uiManager->addContactToUI(contact);
    } else {
      uiManager->updateContactLastSeen(contact.id.pub_key, contact.lastmod);
    }
    saveContacts();
  }

  void onContactPathUpdated(const ContactInfo& contact) override {
    Serial.printf("PATH to: %s, path_len=%d\n", contact.name, (int32_t) contact.out_path_len);
    saveContacts();
  }

  ContactInfo* processAck(const uint8_t *data) override {
    if (memcmp(data, &expected_ack_crc, 4) == 0) {     // got an ACK from recipient
      Serial.printf("   Got ACK! (round trip: %d millis)\n", _ms->getMillis() - last_msg_sent);
      // NOTE: the same ACK can be received multiple times!
      expected_ack_crc = 0;  // reset our expected hash, now that we have received ACK
      if (uiManager) uiManager->setSendStatus(1);
      return NULL;  // TODO: really should return ContactInfo pointer
    }

    //uint32_t crc;
    //memcpy(&crc, data, 4);
    //MESH_DEBUG_PRINTLN("unknown ACK received: %08X (expected: %08X)", crc, expected_ack_crc);
    return NULL;
  }

  void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
    Serial.printf("(%s) MSG -> from %s\n", pkt->isRouteDirect() ? "DIRECT" : "FLOOD", from.name);
    Serial.printf("   %s\n", text);

    setClock(sender_timestamp);

    if (strcmp(text, "clock sync") == 0) {  // special text command
      setClock(sender_timestamp + 1);
    }

    char time_buf[16];
    format_time(sender_timestamp, time_buf, sizeof(time_buf));

    uiManager->routeIncomingDM(from.id.pub_key, from.name, time_buf, text);
    msgstore_append_dm(from.id.pub_key, sender_timestamp, false, text);
  }

  void onCommandDataRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
    MESH_DEBUG_PRINTLN("onCommandDataRecv");
  }
  void onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t *sender_prefix, const char *text) override {
    MESH_DEBUG_PRINTLN("onSignedMessageRecv");
  }

  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char *text) override {
    if (pkt->isRouteDirect()) {
      Serial.printf("PUBLIC CHANNEL MSG -> (Direct!)\n");
    } else {
      Serial.printf("PUBLIC CHANNEL MSG -> (Flood) hops %d\n", pkt->path_len);
    }
    Serial.printf("   %s\n", text);

    setClock(timestamp);

    // Only public?
    // if (strcmp(channel.secret, PUBLIC_GROUP_PSK) != 0)
    //      return;

    if (pkt->getPayloadType() != PAYLOAD_TYPE_GRP_TXT)
        return;

    char time_buf[16];
    format_time(timestamp, time_buf, sizeof(time_buf));

    char sender[32];
    char msg[192];

    parse_group_message(text, sender, sizeof(sender), msg, sizeof(msg));

    bool is_self = (strcmp(sender, _prefs.node_name) == 0);

    uiManager->addChatBubble(time_buf, sender, msg, is_self);
    msgstore_append_public(timestamp, sender, is_self, msg);
  }

  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) override {
    MESH_DEBUG_PRINTLN("onContactRequest");
    // TODO: Return telemetry data
    return 0; // unknown
  }

  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override {
    MESH_DEBUG_PRINTLN("onContactResponse");
    // not supported
  }

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override {
    return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
  }
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override {
    return SEND_TIMEOUT_BASE_MILLIS + 
         ( (pkt_airtime_millis*DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) * (path_len + 1));
  }

  void onSendTimeout() override {
    Serial.println("   ERROR: timed out, no ACK.");
    if (uiManager) uiManager->setSendStatus(2);
  }

public:
  MyMesh(mesh::Radio& radio, StdRNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables)
     : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables)
  {
    // defaults
    memset(&_prefs, 0, sizeof(_prefs));
    _prefs.airtime_factor = 2.0;    // one third    
    #ifdef ADVERT_NAME
      strcpy(_prefs.node_name, ADVERT_NAME);
    #else
      strcpy(_prefs.node_name, "NONAME");
    #endif
    _prefs.freq = LORA_FREQ;
    _prefs.tx_power_dbm = LORA_TX_POWER;
    _prefs.bw = LORA_BW;
    _prefs.sf = LORA_SF;
    _prefs.cr = LORA_CR;

    command[0] = 0;
    curr_recipient = NULL;
  }
  const char* getFirmwareVer() { return FIRMWARE_VERSION; }
  const char* getBuildDate() { return FIRMWARE_BUILD_DATE; }
  const char* getRole() { return FIRMWARE_ROLE; }
  const char* getNodeName() { return _prefs.node_name; }
  float getFreqPref() const { return _prefs.freq; }
  float getBwPref() const { return _prefs.bw; }
  uint8_t getSfPref() const { return _prefs.sf; }
  uint8_t getCrPref() const { return _prefs.cr; }
  uint8_t getTxPowerPref() const { return _prefs.tx_power_dbm; }

  void begin(FILESYSTEM& fs) {
    _fs = &fs;

    BaseChatMesh::begin();

  #if defined(NRF52_PLATFORM)
    IdentityStore store(fs, "");
  #elif defined(RP2040_PLATFORM)
    IdentityStore store(fs, "/identity");
    store.begin();
  #else
    IdentityStore store(fs, "/identity");
  #endif
    if (!store.load("_main", self_id, _prefs.node_name, sizeof(_prefs.node_name))) {  // legacy: node_name was from identity file
      // Need way to get some entropy to seed RNG
      // Serial.println("Press ENTER to generate key:");
      // char c = 0;
      // while (c != '\n') {   // wait for ENTER to be pressed
      //   if (Serial.available()) c = Serial.read();
      // }
      ((StdRNG *)getRNG())->begin(millis());

      self_id = mesh::LocalIdentity(getRNG());  // create new random identity
      int count = 0;
      while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
        self_id = mesh::LocalIdentity(getRNG()); count++;
      }
      store.save("_main", self_id);
    }

    // load persisted prefs
    if (_fs->exists("/node_prefs")) {
    #if defined(RP2040_PLATFORM)
      File file = _fs->open("/node_prefs", "r");
    #else
      File file = _fs->open("/node_prefs");
    #endif
      if (file) {
        file.read((uint8_t *) &_prefs, sizeof(_prefs));
        file.close();
      }
    }
    // Apply defaults for fields that were zero in old saved files
    if (_prefs.bw == 0.0f) _prefs.bw = LORA_BW;
    if (_prefs.sf == 0)    _prefs.sf = LORA_SF;
    if (_prefs.cr == 0)    _prefs.cr = LORA_CR;

    loadContacts();
    _public = addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure Andy's public channel
  }

  void savePrefs() {
#if defined(NRF52_PLATFORM)
    _fs->remove("/node_prefs");
    File file = _fs->open("/node_prefs", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File file = _fs->open("/node_prefs", "w");
#else
    File file = _fs->open("/node_prefs", "w", true);
#endif
    if (file) {
      file.write((const uint8_t *)&_prefs, sizeof(_prefs));
      file.close();
    }
  }

  void showWelcome() {
    Serial.println("===== MeshCore Chat Terminal =====");
    Serial.println();
    Serial.printf("WELCOME  %s\n", _prefs.node_name);
    mesh::Utils::printHex(Serial, self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();
    Serial.println("   (enter 'help' for basic commands)");
    Serial.println();
  }

  void sendSelfAdvert(int delay_millis) {
    auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
    if (pkt) {
      sendFlood(pkt, delay_millis);
    }
  }

  // ContactVisitor
  void onContactVisit(const ContactInfo& contact) override {
    Serial.printf("   %s - ", contact.name);
    char tmp[40];
    int32_t secs = contact.last_advert_timestamp - getRTCClock()->getCurrentTime();
    AdvertTimeHelper::formatRelativeTimeDiff(tmp, secs, false);
    Serial.println(tmp);
  }

  void handleCommand(const char* command) {
    while (*command == ' ') command++;  // skip leading spaces

    if (memcmp(command, "send ", 5) == 0) {
      if (curr_recipient) {
        const char *text = &command[5];
        uint32_t est_timeout;

        int result = sendMessage(*curr_recipient, getRTCClock()->getCurrentTime(), 0, text, expected_ack_crc, est_timeout);
        if (result == MSG_SEND_FAILED) {
          Serial.println("   ERROR: unable to send.");
        } else {
          last_msg_sent = _ms->getMillis();
          Serial.printf("   (message sent - %s)\n", result == MSG_SEND_SENT_FLOOD ? "FLOOD" : "DIRECT");
        }
      } else {
        Serial.println("   ERROR: no recipient selected (use 'to' cmd).");
      }
    } else if (memcmp(command, "public ", 7) == 0) {  // send GroupChannel msg
      uint8_t temp[5+MAX_TEXT_LEN+32];
      uint32_t timestamp = getRTCClock()->getCurrentTime();
      memcpy(temp, &timestamp, 4);   // mostly an extra blob to help make packet_hash unique
      temp[4] = 0;  // attempt and flags

      sprintf((char *) &temp[5], "%s: %s", _prefs.node_name, &command[7]);  // <sender>: <msg>
      temp[5 + MAX_TEXT_LEN] = 0;  // truncate if too long

      int len = strlen((char *) &temp[5]);
      auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, _public->channel, temp, 5 + len);
      if (pkt) {
        sendFlood(pkt);
        Serial.println("   Sent.");
      } else {
        Serial.println("   ERROR: unable to send");
      }
    } else if (memcmp(command, "list", 4) == 0) {  // show Contact list, by most recent
      int n = 0;
      if (command[4] == ' ') {  // optional param, last 'N'
        n = atoi(&command[5]);
      }
      scanRecentContacts(n, this);
    } else if (strcmp(command, "clock") == 0) {    // show current time
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      Serial.printf(   "%02d:%02d - %d/%d/%d UTC\n", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else if (memcmp(command, "time ", 5) == 0) {  // set time (to epoch seconds)
      uint32_t secs = _atoi(&command[5]);
      setClock(secs);
    } else if (memcmp(command, "to ", 3) == 0) {  // set current recipient
      curr_recipient = searchContactsByPrefix(&command[3]);
      if (curr_recipient) {
        Serial.printf("   Recipient %s now selected.\n", curr_recipient->name);
      } else {
        Serial.println("   Error: Name prefix not found.");
      }
    } else if (strcmp(command, "to") == 0) {    // show current recipient
      if (curr_recipient) {
         Serial.printf("   Current: %s\n", curr_recipient->name);
      } else {
         Serial.println("   Err: no recipient selected");
      }
    } else if (strcmp(command, "advert") == 0) {
      auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
      if (pkt) {
        sendZeroHop(pkt);
        Serial.println("   (advert sent, zero hop).");
      } else {
        Serial.println("   ERR: unable to send");
      }
    } else if (strcmp(command, "reset path") == 0) {
      if (curr_recipient) {
        resetPathTo(*curr_recipient);
        saveContacts();
        Serial.println("   Done.");
      }
    } else if (memcmp(command, "card", 4) == 0) {
      Serial.printf("Hello %s\n", _prefs.node_name);
      auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
      if (pkt) {
        uint8_t len =  pkt->writeTo(tmp_buf);
        releasePacket(pkt);  // undo the obtainNewPacket()

        mesh::Utils::toHex(hex_buf, tmp_buf, len);
        Serial.println("Your MeshCore biz card:");
        Serial.print("meshcore://"); Serial.println(hex_buf);
        Serial.println();
      } else {
        Serial.println("  Error");
      }
    } else if (memcmp(command, "import ", 7) == 0) {
      importCard(&command[7]);
    } else if (memcmp(command, "set ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "af ", 3) == 0) {
        _prefs.airtime_factor = atof(&config[3]);
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "name ", 5) == 0) {
        StrHelper::strncpy(_prefs.node_name, &config[5], sizeof(_prefs.node_name));
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "lat ", 4) == 0) {
        _prefs.node_lat = atof(&config[4]);
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "lon ", 4) == 0) {
        _prefs.node_lon = atof(&config[4]);
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "tx ", 3) == 0) {
        _prefs.tx_power_dbm = atoi(&config[3]);
        savePrefs();
        Serial.println("  OK - reboot to apply");
      } else if (memcmp(config, "freq ", 5) == 0) {
        _prefs.freq = atof(&config[5]);
        savePrefs();
        Serial.println("  OK - reboot to apply");
      } else if (memcmp(config, "bw ", 3) == 0) {
        _prefs.bw = atof(&config[3]);
        savePrefs();
        Serial.println("  OK - reboot to apply");
      } else if (memcmp(config, "sf ", 3) == 0) {
        _prefs.sf = (uint8_t)atoi(&config[3]);
        savePrefs();
        Serial.println("  OK - reboot to apply");
      } else if (memcmp(config, "cr ", 3) == 0) {
        _prefs.cr = (uint8_t)atoi(&config[3]);
        savePrefs();
        Serial.println("  OK - reboot to apply");
      } else {
        Serial.printf("  ERROR: unknown config: %s\n", config);
      }
    } else if (memcmp(command, "ver", 3) == 0) {
      Serial.println(FIRMWARE_VER_TEXT);
    } else if (memcmp(command, "help", 4) == 0) {
      Serial.println("Commands:");
      Serial.println("   set {name|lat|lon|freq|tx|af} {value}");
      Serial.println("   card");
      Serial.println("   import {biz card}");
      Serial.println("   clock");
      Serial.println("   time <epoch-seconds>");
      Serial.println("   list {n}");
      Serial.println("   to <recipient name or prefix>");
      Serial.println("   to");
      Serial.println("   send <text>");
      Serial.println("   advert");
      Serial.println("   reset path");
      Serial.println("   public <text>");
    } else {
      Serial.print("   ERROR: unknown command: "); Serial.println(command);
    }
  }

  void loop() {
    BaseChatMesh::loop();

    int len = strlen(command);
    while (Serial.available() && len < sizeof(command)-1) {
      char c = Serial.read();
      if (c != '\n') { 
        command[len++] = c;
        command[len] = 0;
      }
      Serial.print(c);
    }
    if (len == sizeof(command)-1) {  // command buffer full
      command[sizeof(command)-1] = '\r';
    }

    if (len > 0 && command[len - 1] == '\r') {  // received complete line
      command[len - 1] = 0;  // replace newline with C string null terminator

      handleCommand(command);
      command[0] = 0;  // reset command buffer
    }
  }
};

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

void configureDisplay() {
  ESP_LOGI(TAG, "Configuring display...");

  screenWidth = lcd.width();
  screenHeight = lcd.height();

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, screenWidth * screenHeight / 10);

  /* Initialize the display */
  lv_disp_drv_init(&disp_drv);
  /* Change the following line to your display resolution */
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  /* Initialize the (dummy) input device driver */
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  lcd.fillScreen(0x000000u);
}

// ── ROOT CAUSE NOTE ──────────────────────────────────────────────────────────
// Upstream LovyanGFX 1.2.7 does NOT understand the IO_EXPANDER pin encoding
// (only the mverch67 fork does).  Our SCIndicatorDisplay.h sets
//   cfg.pin_cs = 4 | IO_EXPANDER  (= 0x44 = 68)
// During Panel_ST7701_Base::init(), LovyanGFX calls
//   lgfx::gpio_lo(pin_cs)  // assert CS
//   ... send SPI init commands (gamma, voltages, RGB666, sleep-out, etc.) ...
//   lgfx::gpio_hi(pin_cs)  // deassert CS
// gpio_lo(68) on ESP32-S3 (max GPIO 48) is a silent no-op → CS is NEVER
// actually asserted → all SPI init commands are sent into the void →
// the ST7701 never receives its init sequence.
//
// This worked SOMETIMES because:
//   - on soft reboot the ST7701 retained state from the previous boot
//   - on cold boot from charged caps it was in some semi-init state
//   - on cold boot from discharged caps it stayed in factory state → stripes
//
// FIX: manually assert TCA9535 P0.4 (real LCD CS) LOW before lcd.begin()
// and deassert it HIGH after. CS stays LOW for the entire LovyanGFX init,
// so all SPI commands actually reach the ST7701. The internal gpio_lo/hi
// calls in LovyanGFX become harmless no-ops (they target a non-existent
// GPIO 68, but our manual TCA9535 CS state is preserved).
// ────────────────────────────────────────────────────────────────────────────

static void sensecap_lcd_cs_assert() {
  const uint8_t TCA = 0x20;
  // Config Port 0: bit 4 (LCD CS) = OUTPUT, bit 5 (LCD RESX) = OUTPUT,
  // others = INPUT.  0xCF = 1100 1111 (bits 4,5 cleared = output).
  Wire.beginTransmission(TCA);
  Wire.write(0x06);   // CONFIG_P0
  Wire.write(0xCF);
  uint8_t e1 = Wire.endTransmission();

  // Output Port 0: bit 4 LOW (CS asserted), bit 5 HIGH (RESX deasserted),
  // all other latches HIGH.  0xEF = 1110 1111.
  Wire.beginTransmission(TCA);
  Wire.write(0x02);   // OUTPUT_P0
  Wire.write(0xEF);
  uint8_t e2 = Wire.endTransmission();

  Serial.printf("[lcd_cs] assert LOW: cfg_err=%u out_err=%u\n", e1, e2);
}

static void sensecap_lcd_cs_deassert() {
  const uint8_t TCA = 0x20;
  // Output Port 0: all HIGH (CS deasserted, RESX deasserted).
  Wire.beginTransmission(TCA);
  Wire.write(0x02);   // OUTPUT_P0
  Wire.write(0xFF);
  uint8_t e = Wire.endTransmission();
  Serial.printf("[lcd_cs] deassert HIGH: out_err=%u\n", e);
}

void initializeDisplay() {
  ESP_LOGI(TAG, "Initializing display...");

  // Hold the LCD CS LOW manually for the entire lcd.begin() duration so the
  // ST7701 actually receives the SPI init sequence.  See ROOT CAUSE NOTE above.
  sensecap_lcd_cs_assert();
  delay(5);

  bool ok = lcd.begin();
  Serial.printf("[display] lcd.begin() = %s  w=%d h=%d\n", ok ? "OK" : "FAIL", lcd.width(), lcd.height());

  // Release LCD CS now that the SPI init sequence is complete.  Subsequent
  // RGB pixel data goes via the parallel bus and does not need CS.
  sensecap_lcd_cs_deassert();

  lcd.setBrightness(255);
  // Fallback: force GPIO 45 HIGH in case LovyanGFX Light_PWM LEDC setup failed.
  pinMode(45, OUTPUT);
  digitalWrite(45, HIGH);
  Serial.printf("[display] BL GPIO45 after force HIGH: %d\n", digitalRead(45));
  lcd.setTextSize(2);
  lcd.setRotation(0);
}

void initializeTouchScreen() {
  ESP_LOGI(TAG, "Initializing touch screen...");
  touch_init();
}

void initializeLVGL() {
  ESP_LOGI(TAG, "Initializing LVGL...");
  lv_init();
}

void initializeMesh() {
  Serial.println("[mesh] board.begin()");
  board.begin();

#ifndef DISABLE_LORA_FOR_DISPLAY_TEST
  Serial.println("[mesh] radio_init()...");
  if (!radio_init()) {
    Serial.println("[mesh] FATAL: radio_init() failed - halting");
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());
#else
  Serial.println("[mesh] *** LORA DISABLED FOR DISPLAY TEST ***");
  fast_rng.begin(42);
#endif

  Serial.println("[mesh] SPIFFS.begin()");
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  the_mesh.begin(InternalFS);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  the_mesh.begin(LittleFS);
#elif defined(ESP32)
  SPIFFS.begin(true);
  the_mesh.begin(SPIFFS);
#else
  #error "need to define filesystem"
#endif
  Serial.println("[mesh] the_mesh.begin() done");

  float freq  = the_mesh.getFreqPref();
  float bw    = the_mesh.getBwPref();
  uint8_t sf  = the_mesh.getSfPref();
  uint8_t cr  = the_mesh.getCrPref();
  // Always use the build-defined TX power; saved prefs may contain a stale
  // value from a previous firmware version with a different default.
  uint8_t txpwr = LORA_TX_POWER;
  Serial.printf("[mesh] Radio params: freq=%.3f MHz  BW=%.1f  SF=%d  CR=%d  TX=%d dBm\n",
                freq, bw, (int)sf, (int)cr, (int)txpwr);
  radio_set_params(freq, bw, sf, cr);
  radio_set_tx_power(txpwr);

  the_mesh.showWelcome();

  uiManager->populateSettings(the_mesh.getNodeName(),
                              the_mesh.getFreqPref(),
                              the_mesh.getBwPref(),
                              the_mesh.getSfPref(),
                              the_mesh.getCrPref(),
                              the_mesh.getTxPowerPref(),
                              the_mesh.getFirmwareVer(),
                              the_mesh.getBuildDate());

  char pk_hex[17];
  mesh::Utils::toHex(pk_hex, the_mesh.self_id.pub_key, 8);
  pk_hex[16] = 0;
  uiManager->populateHome(the_mesh.getNodeName(),
                          pk_hex,
                          the_mesh.getNumContacts(),
                          the_mesh.getFreqPref());
  uiManager->setMyNodeName(the_mesh.getNodeName());

  uiManager->beginPublicHistoryLoad();
  msgstore_load_public();
  uiManager->endPublicHistoryLoad();
  uiManager->scrollPublicChatToBottom();  // after flex layout is recalculated

  Serial.printf("[ui] free heap: %u B  free PSRAM: %u B\n",
                esp_get_free_heap_size(), esp_get_free_internal_heap_size());

  Serial.println("[mesh] Sending self-advert...");
  the_mesh.sendSelfAdvert(1200);
  Serial.println("[mesh] Advert queued (1200ms delay)");
}

void setup() {
  Serial.begin(115200);

  // Greece: UTC+2 standard (EET), UTC+3 summer (EEST)
  // Change this string to match your timezone if needed.
  setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
  tzset();

#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  // Initialise I2C early so both the touch controller (FT5x06 @ 0x48)
  // and the IO expander (TCA9535 @ 0x20) are ready before lcd.begin().
  Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL, 400000);
#endif

  initializeDisplay();
  delay(200);

  initializeLVGL();
  initializeTouchScreen();
  configureDisplay();

  initializeUI();
  
  createTasks();

  // ── Let LVGL render the initial UI before suspending ───────────────────────
  // configureDisplay() fills the RGB framebuffer with black.  LVGL needs at
  // least one lv_timer_handler() cycle to paint the initial screen into that
  // framebuffer.  Since lvgl_task (Core 0) runs in parallel with setup()
  // (Core 1), a short delay here gives it time to complete the first render,
  // so the display shows the UI rather than a blank screen during radio init.
  delay(100);

  // ── Suspend LVGL task before radio init ────────────────────────────────────
  // radio_init() calls Wire.end()/Wire.begin() while the LVGL touch callback
  // (my_touchpad_read) also accesses Wire.  Suspending lvgl_task eliminates
  // the race; the I2C mutex (g_i2c_mutex) guards all Wire access once it is
  // created inside radio_init().
  Serial.println("[setup] Suspending LVGL task for radio init...");
  vTaskSuspend(t_core0_lvgl);

  initializeMesh();

  // Mark entire LVGL screen dirty so the first lv_timer_handler() tick
  // after resume flushes the full UI.
  lv_obj_invalidate(lv_scr_act());

  Serial.println("[setup] Resuming LVGL task...");
  vTaskResume(t_core0_lvgl);

  vTaskResume(t_core1_core);

  Serial.println("Setup completed");

  Serial.print("MeshCore ");
  Serial.println(the_mesh.getFirmwareVer());
  Serial.print("Build date: ");
  Serial.println(the_mesh.getBuildDate());
}

void handleCommand(char *msg)
{
  Serial.println("Outgoing data:");
  Serial.println(msg);
  the_mesh.handleCommand(msg);
}

void core_task(void *pvParameters) {

  vTaskSuspend(NULL);

  Serial.printf("[core_task] Mesh loop started on core %d\n", xPortGetCoreID());

#ifdef PIN_USER_BTN
  bool btn_was_pressed = false;
#endif

  while (1) {
    the_mesh.loop();
    rtc_clock.tick();

#ifdef PIN_USER_BTN
    bool btn_pressed = (digitalRead(PIN_USER_BTN) == LOW);
    if (btn_pressed && !btn_was_pressed) {
      Serial.println("[btn] Press detected - restarting...");
      delay(50);
      ESP.restart();
    }
    btn_was_pressed = btn_pressed;
#endif

    vTaskDelay(DELAY_CORE_TASK / portTICK_PERIOD_MS);
  }
}

void loop() {
   vTaskDelete(NULL);
}