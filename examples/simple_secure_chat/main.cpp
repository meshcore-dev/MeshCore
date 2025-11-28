#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

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

#ifndef MAX_CONTACTS
  #define MAX_CONTACTS         100
#endif

#include <helpers/BaseChatMesh.h>

#define SEND_TIMEOUT_BASE_MILLIS          500
#define FLOOD_SEND_TIMEOUT_FACTOR         16.0f
#define DIRECT_SEND_PERHOP_FACTOR         6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS   250

#define  PUBLIC_GROUP_PSK  "izOH6cXN6mrJ5e26oRXNcg=="

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

struct NodePrefs {  // persisted to file
  float airtime_factor;
  char node_name[32];
  double node_lat, node_lon;
  float freq;
  uint8_t tx_power_dbm;
  uint8_t lora_sf;
  float lora_bw;        // <--- change from uint32_t to float for decimal support
  uint8_t terminal_mode; // 0 = text, 1 = data (persisted)
  uint8_t unused[1];
  uint64_t last_epoch;   // <--- changed to 64-bit epoch (seconds since epoch)
};

class Terminal {
  static constexpr int CMD_BUF_SIZE = 512+10;
  char command[CMD_BUF_SIZE];
  int cmd_len = 0;
  bool in_prompt = false;

public:
  Terminal() { command[0] = 0; }

  // Draws a BBS-style prompt
  void showPrompt() {
    Serial.print("\r\n> ");
    in_prompt = true;
  }

  // Handles input, returns true if a full command is ready
  bool pollInput() {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\b' || c == 127) { // backspace
        if (cmd_len > 0) {
          cmd_len--;
          command[cmd_len] = 0;
          // Erase char visually
          Serial.print("\b \b");
        }
      } else if (c == '\r' || c == '\n') {
        Serial.print("\r\n");
        command[cmd_len] = 0;
        cmd_len = 0;
        in_prompt = false;
        // consume any immediate extra newline/CR that may follow (e.g. CRLF)
        while (Serial.available() && (Serial.peek() == '\r' || Serial.peek() == '\n')) {
          Serial.read();
        }
        return true;
      } else if (c >= 32 && c < 127 && cmd_len < CMD_BUF_SIZE-1) {
        command[cmd_len++] = c;
        command[cmd_len] = 0;
        Serial.print(c);
      }
      // ignore other control chars
    }
    return false;
  }

  const char* getCommand() const { return command; }
  void clear() { command[0] = 0; cmd_len = 0; }
  bool needsPrompt() const { return !in_prompt; }
};

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

  uint8_t lora_sf;
  float lora_bw;

  // --- Noise floor history (5 minutes, 10s increments) ---
  static constexpr int NOISE_HISTORY_SECONDS = 300; // 5 minutes
  static constexpr int NOISE_HISTORY_STEP = 10;     // 10 seconds
  static constexpr int NOISE_HISTORY_SIZE = NOISE_HISTORY_SECONDS / NOISE_HISTORY_STEP;

  int16_t noise_history[NOISE_HISTORY_SIZE] = {0};
  uint32_t noise_history_time[NOISE_HISTORY_SIZE] = {0};
  int noise_history_idx = 0;
  unsigned long last_noise_sample = 0;

  // persist-last-epoch bookkeeping (ms timestamp of last prefs write)
  unsigned long last_epoch_persist_ms = 1764342072;
  
  void sampleNoise() {
    unsigned long now = millis();
    if (now - last_noise_sample >= NOISE_HISTORY_STEP * 1000UL) {
      last_noise_sample = now;
      int16_t noise = _radio->getNoiseFloor();
      noise_history[noise_history_idx] = noise;
      noise_history_time[noise_history_idx] = getRTCClock()->getCurrentTime();
      noise_history_idx = (noise_history_idx + 1) % NOISE_HISTORY_SIZE;
    }
  }

  void printNoiseHistory() {
    Serial.println("Noise floor history (last 5 minutes, 10s increments):");
    int idx = noise_history_idx;
    for (int i = 0; i < NOISE_HISTORY_SIZE; ++i) {
      idx = (idx + 1) % NOISE_HISTORY_SIZE;
      if (noise_history_time[idx] != 0) {
        DateTime dt = DateTime(noise_history_time[idx]);
        Serial.printf("%02d:%02d:%02d - Noise: %d dBm\n", dt.hour(), dt.minute(), dt.second(), noise_history[idx]);
      }
    }
  }

  Terminal terminal; // <-- Add this line

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
          uint32_t reserved;

          bool success = (file.read(pub_key, 32) == 32);
          success = success && (file.read((uint8_t *) &c.name, 32) == 32);
          success = success && (file.read(&c.type, 1) == 1);
          success = success && (file.read(&c.flags, 1) == 1);
          success = success && (file.read(&unused, 1) == 1);
          success = success && (file.read((uint8_t *) &reserved, 4) == 4);
          success = success && (file.read((uint8_t *) &c.out_path_len, 1) == 1);
          success = success && (file.read((uint8_t *) &c.last_advert_timestamp, 4) == 4);
          success = success && (file.read(c.out_path, 64) == 64);
          c.gps_lat = c.gps_lon = 0;   // not yet supported

          if (!success) break;  // EOF

          c.id = mesh::Identity(pub_key);
          c.lastmod = 0;
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
      uint32_t reserved = 0;

      while (iter.hasNext(this, c)) {
        bool success = (file.write(c.id.pub_key, 32) == 32);
        success = success && (file.write((uint8_t *) &c.name, 32) == 32);
        success = success && (file.write(&c.type, 1) == 1);
        success = success && (file.write(&c.flags, 1) == 1);
        success = success && (file.write(&unused, 1) == 1);
        success = success && (file.write((uint8_t *) &reserved, 4) == 4);
        success = success && (file.write((uint8_t *) &c.out_path_len, 1) == 1);
        success = success && (file.write((uint8_t *) &c.last_advert_timestamp, 4) == 4);
        success = success && (file.write(c.out_path, 64) == 64);

        if (!success) break;  // write failed
      }
      file.close();
    }
  }

  void setClock(uint32_t timestamp) {
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (timestamp > curr) {
      getRTCClock()->setCurrentTime(timestamp);
      Serial.println("   (OK - clock set!)");
    } else {
      Serial.println("   (ERR: clock cannot go backwards)");
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

  void applyRadioParams() {
    radio_set_params(getFreqPref(), lora_bw, lora_sf, LORA_CR);
    radio_set_tx_power(getTxPowerPref());
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
    float snr = radio_driver.getLastSNR(); // Get SNR for last received packet

    if (_prefs.terminal_mode) {
      char pubhex[ (PUB_KEY_SIZE*2) + 1 ];
      mesh::Utils::toHex(pubhex, contact.id.pub_key, PUB_KEY_SIZE);
      char esc_name[64];
      json_escape(contact.name, esc_name, sizeof(esc_name));
      Serial.printf("{\"event\":\"discovery\",\"name\":\"%s\",\"type\":\"%s\",\"pub_key\":\"%s\",\"snr\":%.1f,\"path_len\":%u,\"new\":%s}\n",
                    esc_name, getTypeName(contact.type), pubhex, snr, (unsigned)path_len, is_new ? "true" : "false");
    } else {
      Serial.printf("[Discovery] %s\n", contact.name);
      Serial.printf("   Type: %s\n", getTypeName(contact.type));
      Serial.printf("   Public Key: ");
      mesh::Utils::printHex(Serial, contact.id.pub_key, PUB_KEY_SIZE);
      Serial.println();
      Serial.printf("   SNR: %.1f dB\n", snr);
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
      return NULL;  // TODO: really should return ContactInfo pointer 
    }

    //uint32_t crc;
    //memcpy(&crc, data, 4);
    //MESH_DEBUG_PRINTLN("unknown ACK received: %08X (expected: %08X)", crc, expected_ack_crc);
    return NULL;
  }

  void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
    float snr = radio_driver.getLastSNR();
    if (_prefs.terminal_mode) {
      char esc_from[64]; json_escape(from.name, esc_from, sizeof(esc_from));
      char esc_text[512]; json_escape(text ? text : "", esc_text, sizeof(esc_text));
      const char* route = pkt->isRouteDirect() ? "direct" : "flood";
      Serial.printf("{\"event\":\"message\",\"from\":\"%s\",\"route\":\"%s\",\"timestamp\":%lu,\"text\":\"%s\",\"snr\":%.1f}\n",
                    esc_from, route, (unsigned long)sender_timestamp, esc_text, snr);
    } else {
      Serial.printf("(%s) MSG -> from %s\n", pkt->isRouteDirect() ? "DIRECT" : "FLOOD", from.name);
      Serial.printf("   %s\n", text);
      if (strcmp(text, "clock sync") == 0) {  // special text command
        setClock(sender_timestamp + 1);
      }
    }
  }

  void onCommandDataRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
  }
  void onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t *sender_prefix, const char *text) override {
  }

  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char *text) override {
    float snr = radio_driver.getLastSNR(); // Get SNR for last received packet
    if (_prefs.terminal_mode) {
      char esc_text[512]; json_escape(text ? text : "", esc_text, sizeof(esc_text));
      // GroupChannel does not expose a 'name' member on all platforms; use a stable literal
      // (most use-case here is the Public channel). If you need the numeric id instead,
      // replace "public" with a numeric field derived from your ChannelDetails reference.
      Serial.printf("{\"event\":\"channel_message\",\"channel\":\"public\",\"hops\":%u,\"text\":\"%s\",\"snr\":%.1f}\n",
                    (unsigned)pkt->path_len, esc_text, snr);
    } else {
      if (pkt->isRouteDirect()) {
        Serial.printf("[Public] %s (SNR: %.1f dB)\n", text, snr);
      } else {
        if (pkt->path_len > 0) {
          Serial.printf("[Public] (via %d hops): %s (SNR: %.1f dB)\n", pkt->path_len, text, snr);
        } else {
          Serial.printf("[Public]: %s (SNR: %.1f dB)\n", text, snr);
        }
      }
    }
  }

  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) override {
    return 0;  // unknown
  }

  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override {
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
  }

public:
  MyMesh(mesh::Radio& radio, StdRNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables)
     : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables)
  {
    // defaults
    memset(&_prefs, 0, sizeof(_prefs));
    _prefs.airtime_factor = 2.0;    // one third
    strcpy(_prefs.node_name, "NONAME");
    _prefs.freq = LORA_FREQ;
    _prefs.tx_power_dbm = LORA_TX_POWER;
    _prefs.lora_sf = LORA_SF;
    _prefs.lora_bw = LORA_BW;       // default as float
    _prefs.terminal_mode = 0;       // default to text mode

    lora_sf = _prefs.lora_sf;
    lora_bw = _prefs.lora_bw;

    command[0] = 0;
    curr_recipient = NULL;
  }

  float getFreqPref() const { return _prefs.freq; }
  uint8_t getTxPowerPref() const { return _prefs.tx_power_dbm; }
  float getLoraBW() const { return lora_bw; }
  uint8_t getLoraSF() const { return lora_sf; }

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
    if (!store.load("_main", self_id, _prefs.node_name, sizeof(_prefs.node_name))) {
      // Need way to get some entropy to seed RNG
      Serial.println("Press ENTER to generate key:");
      char c = 0;
      while (c != '\n') {   // wait for ENTER to be pressed
        if (Serial.available()) c = Serial.read();
      }
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

    // initialize last-epoch persistence timer
    last_epoch_persist_ms = millis();

    // If we have a saved epoch in NVRAM, use it to initialise the RTC.
    // Only set the RTC if the saved epoch is non-zero and RTC is unset (0)
    // or differs by more than one hour.
    if (_prefs.last_epoch != 0ULL) {
      uint32_t saved_epoch = (uint32_t)(_prefs.last_epoch & 0xFFFFFFFFUL);
      uint32_t rtc_now = getRTCClock()->getCurrentTime();
      if (rtc_now == 0 || (int64_t)rtc_now - (int64_t)saved_epoch > 3600 || (int64_t)saved_epoch - (int64_t)rtc_now > 3600) {
        getRTCClock()->setCurrentTime(saved_epoch);
        Serial.printf("   RTC initialised from NVRAM epoch: %lu\n", (unsigned long)saved_epoch);
      }
    }

    // If RTC has time and prefs had no epoch, persist immediately
    {
      uint32_t rtc_now = getRTCClock()->getCurrentTime();
      if (rtc_now > 0 && _prefs.last_epoch == 0ULL) {
        _prefs.last_epoch = (uint64_t)rtc_now;
        savePrefs();
      }
    }

    // --- Load LoRa params from prefs, fallback if not set ---
    if (_prefs.lora_sf >= 6 && _prefs.lora_sf <= 12) {
      lora_sf = _prefs.lora_sf;
    } else {
      lora_sf = LORA_SF;
      _prefs.lora_sf = lora_sf;
    }
    // Accept 62.5, 125, 250, 500 as valid
    if (_prefs.lora_bw == 62.5f || _prefs.lora_bw == 125.0f || _prefs.lora_bw == 250.0f || _prefs.lora_bw == 500.0f) {
      lora_bw = _prefs.lora_bw;
    } else {
      lora_bw = LORA_BW;
      _prefs.lora_bw = lora_bw;
    }
    applyRadioParams();

    loadContacts();
    _public = addChannel("Public", PUBLIC_GROUP_PSK);
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
      // Always update LoRa params before saving
      _prefs.lora_sf = lora_sf;
      _prefs.lora_bw = lora_bw;
      file.write((const uint8_t *)&_prefs, sizeof(_prefs));
      file.close();
    }
  }

  void showWelcome() {
    Serial.println("╔════════════════════════════════════════════════════╗");
    Serial.println("║            MeshCore Chat BBS Terminal             ║");
    Serial.println("╠════════════════════════════════════════════════════╣");
    Serial.printf ("║  Welcome, %-38s║\n", _prefs.node_name);
    Serial.println("╚════════════════════════════════════════════════════╝");
    mesh::Utils::printHex(Serial, self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();
    Serial.println("Type 'help' for commands.");
    Serial.println();
    terminal.showPrompt();
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

  void printSettings() {
    Serial.println("Current MeshCore Settings:");
    Serial.printf("  Name: %s\n", _prefs.node_name);
    Serial.printf("  Frequency: %.3f MHz\n", _prefs.freq); // <-- 3 decimal places
    Serial.printf("  TX Power: %u dBm\n", _prefs.tx_power_dbm);
    Serial.printf("  Airtime Factor: %.2f\n", _prefs.airtime_factor);
    Serial.printf("  Latitude: %.6f\n", _prefs.node_lat);
    Serial.printf("  Longitude: %.6f\n", _prefs.node_lon);
    Serial.printf("  LoRa SF: %u\n", lora_sf);
    Serial.printf("  LoRa BW: %.1f kHz\n", lora_bw); // show decimal
  }

void handleCommand(const char* command) {

    if (_prefs.terminal_mode) {
      handleDataCommand(command);
    } else {
      handleTextCommand(command);
    }
}


  void handleTextCommand(const char* command) {
    while (*command == ' ') command++;  // skip leading spaces

    if (strlen(command) == 0) {
      terminal.showPrompt();
      return;
    }

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
      } else if (memcmp(config, "sf ", 3) == 0) {
        int sf = atoi(&config[3]);
        if (sf >= 6 && sf <= 12) {
          lora_sf = sf;
          _prefs.lora_sf = sf;
          savePrefs();
          applyRadioParams();
          Serial.println("  OK - SF updated & saved");
        } else {
          Serial.println("  ERROR: SF must be 6-12");
        }
      } else if (memcmp(config, "bw ", 3) == 0) {
        float bw = atof(&config[3]);
        // Accept 62.5, 125, 250, 500 only
        if (bw == 62.5f || bw == 125.0f || bw == 250.0f || bw == 500.0f) {
          lora_bw = bw;
          _prefs.lora_bw = lora_bw;
          savePrefs();
          applyRadioParams();
          Serial.println("  OK - BW updated & saved");
        } else {
          Serial.println("  ERROR: BW must be 62.5, 125, 250, or 500");
        }
      } else {
        Serial.printf("  ERROR: unknown config: %s\n", config);
      }
    } else if (memcmp(command, "ping ", 5) == 0) {
      const char* name = &command[5];
      ContactInfo* recipient = searchContactsByPrefix(name);
      if (recipient) {
        uint32_t tag = 0, est_timeout = 0;
        int result = sendRequest(*recipient, REQ_TYPE_GET_STATUS, tag, est_timeout);
        if (result == MSG_SEND_FAILED) {
          Serial.println("   ERROR: unable to send ping.");
        } else {
          Serial.printf("   Ping sent to %s (timeout: %lu ms)\n", recipient->name, (unsigned long)est_timeout);
        }
      } else {
        Serial.println("   ERROR: recipient not found.");
      }
    } else if (memcmp(command, "data", 4) == 0) {
      // data [on|off] - toggle or show DATA (M2M JSON) mode, persisted in prefs
      const char* arg = &command[4];
      while (*arg == ' ') arg++; // skip spaces
      if (*arg == 0) {
        Serial.printf("   DATA mode is %s\n", _prefs.terminal_mode ? "ON" : "OFF");
      } else if (strcmp(arg, "on") == 0) {
        _prefs.terminal_mode = 1;
        savePrefs();
        Serial.println("   DATA mode set to ON (persisted).");
      } else if (strcmp(arg, "off") == 0) {
        _prefs.terminal_mode = 0;
        savePrefs();
        Serial.println("   DATA mode set to OFF (persisted).");
      } else {
        Serial.println("   Usage: data {on|off}");
      }
    } else if (strcmp(command, "shownoise") == 0) {
      printNoiseHistory();
    }
    else if (strcmp(command, "settings") == 0) {
      printSettings();
    } else if (memcmp(command, "ver", 3) == 0) {
      Serial.println(FIRMWARE_VER_TEXT);
    } else if (memcmp(command, "help", 4) == 0) {
      Serial.println("╔════════════════════════════════════════════════════╗");
      Serial.println("║ Commands:                                          ║");
      Serial.println("║   set {name|lat|lon|freq|tx|af|sf|bw} {value}      ║");
      Serial.println("║   card                                             ║");
      Serial.println("║   import {biz card}                                ║");
      Serial.println("║   clock                                            ║");
      Serial.println("║   time <epoch-seconds>                             ║");
      Serial.println("║   list {n}                                         ║");
      Serial.println("║   to <recipient name or prefix>                    ║");
      Serial.println("║   to                                               ║");
      Serial.println("║   send <text>                                      ║");
      Serial.println("║   advert                                           ║");
      Serial.println("║   reset path                                       ║");
      Serial.println("║   public <text>                                    ║");
      Serial.println("║   ping <recipient name or prefix>                  ║");
      Serial.println("║   settings                                         ║");
      Serial.println("╚════════════════════════════════════════════════════╝");
    } else {
      Serial.print("   ERROR: unknown command: "); Serial.println(command);
    }
    terminal.showPrompt();
  }

  // Minimal JSON helpers (in-class, no deps)
  static bool json_get_string(const char* json, const char* key, char* out, int outLen) {
    out[0] = 0;
    if (!json || !key) return false;
    // find the key (quoted or bare)
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) p = strstr(json, key);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    p++; // after colon
    while (*p == ' ' || *p == '\t') p++;
    // quoted string
    if (*p == '"') {
      p++;
      int i = 0;
      while (*p && *p != '"' && i < outLen-1) {
        if (*p == '\\' && *(p+1)) p++; // skip escape prefix
        out[i++] = *p++;
      }
      out[i] = 0;
      return true;
    }
    // unquoted (number, boolean)
    int i = 0;
    while (*p && *p != ',' && *p != '}' && *p != '\n' && i < outLen-1) {
      out[i++] = *p++;
    }
    // trim trailing spaces
    while (i > 0 && out[i-1] == ' ') i--;
    out[i] = 0;
    return i > 0;
  }

  static bool json_get_float(const char* json, const char* key, float* out) {
    char tmp[32];
    if (!json_get_string(json, key, tmp, sizeof(tmp))) return false;
    *out = atof(tmp);
    return true;
  }

  // helper: simple JSON string escaper
  static void json_escape(const char* in, char* out, int outLen) {
    if (!in || outLen <= 0) { if (outLen>0) out[0]=0; return; }
    int oi = 0;
    for (int i = 0; in[i] && oi < outLen-1; ++i) {
      char c = in[i];
      if (c == '"' || c == '\\') {
        if (oi < outLen-2) { out[oi++] = '\\'; out[oi++] = c; }
        else break;
      } else if (c == '\n') {
        if (oi < outLen-3) { out[oi++]='\\'; out[oi++]='n'; }
        else break;
      } else if (c == '\r') {
        if (oi < outLen-3) { out[oi++]='\\'; out[oi++]='r'; }
        else break;
      } else {
        out[oi++] = c;
      }
    }
    out[oi] = 0;
  }

  // Refactored data-mode handler: input JSON, output JSON responses
  void handleDataCommand(const char* command) {
    if (!command) {
      Serial.println("{\"status\":\"error\",\"reason\":\"null_command\"}");
      return;
    }
    // skip leading whitespace
    while (*command == ' ' || *command == '\t' || *command == '\r' || *command == '\n') command++;
    // IGNORE empty input in DATA mode (no JSON error)
    if (*command == 0) {
      return;
    }
    // basic JSON sanity check
    if (*command != '{') {
      Serial.println("{\"status\":\"error\",\"reason\":\"invalid_json\"}");
      return;
    }

    char cmd[64] = {0};
    if (!json_get_string(command, "cmd", cmd, sizeof(cmd))) {
      Serial.println("{\"status\":\"error\",\"reason\":\"missing_cmd\"}");
      return;
    }

    // --- ping: {"cmd":"ping","to":"name"} ---
    if (strcmp(cmd, "ping") == 0) {
      char to[64] = {0};
      if (!json_get_string(command, "to", to, sizeof(to))) {
        Serial.println("{\"status\":\"error\",\"reason\":\"missing_to\"}");
        return;
      }
      ContactInfo* recip = searchContactsByPrefix(to);
      if (!recip) {
        Serial.printf("{\"status\":\"error\",\"reason\":\"recipient_not_found\",\"to\":\"%s\"}\n", to);
        return;
      }
      uint32_t tag = 0, est_timeout = 0;
      int res = sendRequest(*recip, REQ_TYPE_GET_STATUS, tag, est_timeout);
      if (res == MSG_SEND_FAILED) {
        Serial.printf("{\"status\":\"error\",\"reason\":\"send_failed\",\"to\":\"%s\"}\n", recip->name);
      } else {
        Serial.printf("{\"status\":\"ok\",\"action\":\"ping\",\"to\":\"%s\",\"timeout\":%lu}\n",
                      recip->name, (unsigned long)est_timeout);
      }
      return;
    }

    // --- send: {"cmd":"send","to":"name","text":"payload"} ---
    if (strcmp(cmd, "send") == 0) {
      char to[64] = {0};
      char text[512] = {0};
      if (!json_get_string(command, "to", to, sizeof(to))) {
        Serial.println("{\"status\":\"error\",\"reason\":\"missing_to\"}");
        return;
      }
      if (!json_get_string(command, "text", text, sizeof(text))) {
        Serial.println("{\"status\":\"error\",\"reason\":\"missing_text\"}");
        return;
      }
      ContactInfo* recip = searchContactsByPrefix(to);
      if (!recip) {
        Serial.printf("{\"status\":\"error\",\"reason\":\"recipient_not_found\",\"to\":\"%s\"}\n", to);
        return;
      }
      uint32_t est_timeout = 0;
      int r = sendMessage(*recip, getRTCClock()->getCurrentTime(), 0, text, expected_ack_crc, est_timeout);
      if (r == MSG_SEND_FAILED) {
        Serial.printf("{\"status\":\"error\",\"reason\":\"send_failed\",\"to\":\"%s\"}\n", recip->name);
      } else {
        Serial.printf("{\"status\":\"ok\",\"action\":\"send\",\"to\":\"%s\",\"mode\":\"%s\"}\n",
                      recip->name, r == MSG_SEND_SENT_FLOOD ? "flood" : "direct");
      }
      return;
    }

    // --- public: {"cmd":"public","text":"payload"} ---
    if (strcmp(cmd, "public") == 0) {
      char text[512] = {0};
      if (!json_get_string(command, "text", text, sizeof(text))) {
        Serial.println("{\"status\":\"error\",\"reason\":\"missing_text\"}");
        return;
      }
      if (!_public) {
        Serial.println("{\"status\":\"error\",\"reason\":\"no_public_channel\"}");
        return;
      }
      uint8_t temp[5+256+32];
      uint32_t timestamp = getRTCClock()->getCurrentTime();
      memcpy(temp, &timestamp, 4);
      temp[4] = 0;
      snprintf((char*)&temp[5], sizeof(temp)-5, "%s: %s", _prefs.node_name, text);
      int len = strlen((char*)&temp[5]);
      auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, _public->channel, temp, 5 + len);
      if (pkt) {
        sendFlood(pkt);
        Serial.println("{\"status\":\"ok\",\"action\":\"public\"}");
      } else {
        Serial.println("{\"status\":\"error\",\"reason\":\"send_failed\"}");
      }
      return;
    }

    // --- get_settings: {"cmd":"get_settings"} ---
    if (strcmp(cmd, "get_settings") == 0) {
      Serial.printf(
        "{\"status\":\"ok\",\"name\":\"%s\",\"freq\":%.3f,\"tx\":%u,\"sf\":%u,\"bw\":%.1f}\n",
        _prefs.node_name, _prefs.freq, _prefs.tx_power_dbm, lora_sf, lora_bw
      );
      return;
    }

    // --- clock: {"cmd":"clock"} -> returns current epoch
    //           {"cmd":"clock","time":1234567890} -> set clock
    if (strcmp(cmd, "clock") == 0) {
      char tmp[32] = {0};
      if (json_get_string(command, "time", tmp, sizeof(tmp))) {
        unsigned long t = strtoul(tmp, nullptr, 10);
        setClock((uint32_t)t);
        Serial.printf("{\"status\":\"ok\",\"clock_set\":%lu}\n", (unsigned long)t);
      } else {
        uint32_t now = getRTCClock()->getCurrentTime();
        Serial.printf("{\"status\":\"ok\",\"clock\":%lu}\n", (unsigned long)now);
      }
      return;
    }

    // --- set: accept JSON fields same as text 'set' command
    // examples:
    //   {"cmd":"set","name":"NewName"}
    //   {"cmd":"set","lat":51.5074,"lon":-0.1278}
    //   {"cmd":"set","freq":869.618,"tx":22,"sf":8,"bw":62.5,"af":2.0}
    if (strcmp(cmd, "set") == 0) {
      bool changed = false;
      char changed_buf[256]; changed_buf[0] = 0;
      auto add_changed = [&](const char* k){
        if (changed_buf[0]) strncat(changed_buf, ",", sizeof(changed_buf)-strlen(changed_buf)-1);
        strncat(changed_buf, "\"", sizeof(changed_buf)-strlen(changed_buf)-1);
        strncat(changed_buf, k, sizeof(changed_buf)-strlen(changed_buf)-1);
        strncat(changed_buf, "\"", sizeof(changed_buf)-strlen(changed_buf)-1);
      };

      // name
      char tmp_name[sizeof(_prefs.node_name)];
      if (json_get_string(command, "name", tmp_name, sizeof(tmp_name))) {
        StrHelper::strncpy(_prefs.node_name, tmp_name, sizeof(_prefs.node_name));
        add_changed("name"); changed = true;
      }

      // lat
      float fval;
      if (json_get_float(command, "lat", &fval)) {
        _prefs.node_lat = (double)fval;
        add_changed("lat"); changed = true;
      }

      // lon
      if (json_get_float(command, "lon", &fval)) {
        _prefs.node_lon = (double)fval;
        add_changed("lon"); changed = true;
      }

      // freq
      if (json_get_float(command, "freq", &fval)) {
        _prefs.freq = fval;
        add_changed("freq"); changed = true;
      }

      // airtime factor (af)
      if (json_get_float(command, "af", &fval)) {
        _prefs.airtime_factor = fval;
        add_changed("af"); changed = true;
      }

      // tx (integer)
      char tmp[32];
      if (json_get_string(command, "tx", tmp, sizeof(tmp))) {
        int tx = atoi(tmp);
        _prefs.tx_power_dbm = (uint8_t)tx;
        add_changed("tx"); changed = true;
      }

      // sf (spreading factor)
      if (json_get_string(command, "sf", tmp, sizeof(tmp))) {
        int sf = atoi(tmp);
        if (sf >= 6 && sf <= 12) {
          lora_sf = (uint8_t)sf;
          _prefs.lora_sf = lora_sf;
          add_changed("sf"); changed = true;
        } else {
          Serial.printf("{\"status\":\"error\",\"reason\":\"invalid_sf\",\"value\":%d}\n", sf);
          return;
        }
      }

      // bw (bandwidth)
      if (json_get_string(command, "bw", tmp, sizeof(tmp))) {
        float bw = atof(tmp);
        if (bw == 62.5f || bw == 125.0f || bw == 250.0f || bw == 500.0f) {
          lora_bw = bw;
          _prefs.lora_bw = lora_bw;
          add_changed("bw"); changed = true;
        } else {
          Serial.printf("{\"status\":\"error\",\"reason\":\"invalid_bw\",\"value\":%s}\n", tmp);
          return;
        }
      }

      if (changed) {
        savePrefs();
        // apply radio params if relevant fields changed
        applyRadioParams();
        radio_set_tx_power(getTxPowerPref());
        Serial.printf("{\"status\":\"ok\",\"changed\":[%s]}\n", changed_buf);
      } else {
        Serial.println("{\"status\":\"error\",\"reason\":\"no_valid_field_provided\"}");
      }
      return;
    }

    // unknown command
    Serial.printf("{\"status\":\"error\",\"reason\":\"unknown_cmd\",\"cmd\":\"%s\"}\n", cmd);
  }

public:
  void loop() {
    BaseChatMesh::loop();

    sampleNoise();

    // Persist last known RTC epoch to NVRAM once per hour
    {
      uint32_t rtc_now = getRTCClock()->getCurrentTime();
      if (rtc_now > 0) {
        unsigned long now_ms = millis();
        const unsigned long ONE_HOUR_MS = 3600UL * 1000UL;
        if ((now_ms - last_epoch_persist_ms) >= ONE_HOUR_MS) {
          _prefs.last_epoch = (uint64_t)rtc_now;
          savePrefs();
          last_epoch_persist_ms = now_ms;
        }
      }
    }

    if (terminal.pollInput()) {
      handleCommand(terminal.getCommand());
      terminal.clear();
    } else if (terminal.needsPrompt()) {
      terminal.showPrompt();
    }
  }
};

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);

  board.begin();

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

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

  radio_set_params(the_mesh.getFreqPref(), the_mesh.getLoraBW(), the_mesh.getLoraSF(), LORA_CR);
  radio_set_tx_power(the_mesh.getTxPowerPref());

  the_mesh.showWelcome();

  // send out initial Advertisement to the mesh
  the_mesh.sendSelfAdvert(1200);   // add slight delay
}

void loop() {
  the_mesh.loop();
  rtc_clock.tick();
}
