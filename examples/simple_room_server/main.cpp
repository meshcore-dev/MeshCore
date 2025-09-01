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
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/CommonCLI.h>
#include <RTClib.h>
#include <target.h>

/* ------------------------------ Config -------------------------------- */

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "1 Sep 2025"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.8.1"
#endif

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

#ifndef ADVERT_NAME
  #define  ADVERT_NAME   "Test BBS"
#endif
#ifndef ADVERT_LAT
  #define  ADVERT_LAT  0.0
#endif
#ifndef ADVERT_LON
  #define  ADVERT_LON  0.0
#endif

#ifndef ADMIN_PASSWORD
  #define  ADMIN_PASSWORD  "password"
#endif

#ifndef MAX_CLIENTS
 #define MAX_CLIENTS           32
#endif

#ifndef MAX_UNSYNCED_POSTS
  #define MAX_UNSYNCED_POSTS    32
#endif

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY   300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY     200
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

#define FIRMWARE_ROLE "room_server"

#define PACKET_LOG_FILE  "/packet_log"

/* ------------------------------ Code -------------------------------- */

enum RoomPermission {
  ADMIN,
  GUEST,
  READ_ONLY
};

struct ClientInfo {
  mesh::Identity id;
  uint32_t last_timestamp;  // by THEIR clock
  uint32_t last_activity;   // by OUR clock
  uint32_t sync_since;  // sync messages SINCE this timestamp (by OUR clock)
  uint32_t pending_ack;
  uint32_t push_post_timestamp;
  unsigned long ack_timeout;
  RoomPermission  permission;
  uint8_t  push_failures;
  uint8_t  secret[PUB_KEY_SIZE];
  int      out_path_len;
  uint8_t  out_path[MAX_PATH_SIZE];
};

#define MAX_POST_TEXT_LEN    (160-9)

struct PostInfo {
  mesh::Identity author;
  uint32_t post_timestamp;   // by OUR clock
  char text[MAX_POST_TEXT_LEN+1];
};

#define REPLY_DELAY_MILLIS         1500
#define PUSH_NOTIFY_DELAY_MILLIS   2000
#define SYNC_PUSH_INTERVAL         1200

#define PUSH_ACK_TIMEOUT_FLOOD    12000
#define PUSH_TIMEOUT_BASE          4000
#define PUSH_ACK_TIMEOUT_FACTOR    2000

#define POST_SYNC_DELAY_SECS       6

#define CLIENT_KEEP_ALIVE_SECS     0     // Now Disabled (was 128)

#define REQ_TYPE_GET_STATUS          0x01   // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE          0x02
#define REQ_TYPE_GET_TELEMETRY_DATA  0x03

#define RESP_SERVER_LOGIN_OK      0   // response to ANON_REQ

struct ServerStats {
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  int16_t  noise_floor;
  int16_t  last_rssi;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint16_t err_events;          // was 'n_full_events'
  int16_t  last_snr;   // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint16_t n_posted, n_post_push;
};

class MyMesh : public mesh::Mesh, public CommonCLICallbacks {
  FILESYSTEM* _fs;
  unsigned long next_local_advert, next_flood_advert;
  bool _logging;
  NodePrefs _prefs;
  CommonCLI _cli;
  uint8_t reply_data[MAX_PACKET_PAYLOAD];
  int num_clients;
  ClientInfo known_clients[MAX_CLIENTS];
  unsigned long next_push;
  uint16_t _num_posted, _num_post_pushes;
  int next_client_idx;  // for round-robin polling
  int next_post_idx;
  PostInfo posts[MAX_UNSYNCED_POSTS];   // cyclic queue
  CayenneLPP telemetry;
  unsigned long set_radio_at, revert_radio_at;
  float pending_freq;
  float pending_bw;
  uint8_t pending_sf;
  uint8_t pending_cr;

  ClientInfo* putClient(const mesh::Identity& id) {
    for (int i = 0; i < num_clients; i++) {
      if (id.matches(known_clients[i].id)) return &known_clients[i];  // already known
    }
    ClientInfo* newClient;
    if (num_clients < MAX_CLIENTS) {
      newClient = &known_clients[num_clients++];
    } else {    // table is currently full
      // evict least active client
      uint32_t oldest_timestamp = 0xFFFFFFFF;
      newClient = &known_clients[0];
      for (int i = 0; i < num_clients; i++) {
        auto c = &known_clients[i];
        if (c->last_activity < oldest_timestamp) {
          oldest_timestamp = c->last_activity;
          newClient = c;
        }
      }
    }
    newClient->id = id;
    newClient->out_path_len = -1;  // initially out_path is unknown
    newClient->last_timestamp = 0;
    return newClient;
  }

  void  evict(ClientInfo* client) {
    client->last_activity = 0;  // this slot will now be re-used (will be oldest)
    memset(client->id.pub_key, 0, sizeof(client->id.pub_key));
    memset(client->secret, 0, sizeof(client->secret));
    client->pending_ack = 0;
  }

  void addPost(ClientInfo* client, const char* postData) {
    // TODO: suggested postData format: <title>/<descrption>
    posts[next_post_idx].author = client->id;    // add to cyclic queue
    StrHelper::strncpy(posts[next_post_idx].text, postData, MAX_POST_TEXT_LEN);

    posts[next_post_idx].post_timestamp = getRTCClock()->getCurrentTimeUnique();
    next_post_idx = (next_post_idx + 1) % MAX_UNSYNCED_POSTS;

    next_push = futureMillis(PUSH_NOTIFY_DELAY_MILLIS);
    _num_posted++;  // stats
  }

  void pushPostToClient(ClientInfo* client, PostInfo& post) {
    int len = 0;
    memcpy(&reply_data[len], &post.post_timestamp, 4); len += 4;   // this is a PAST timestamp... but should be accepted by client

    uint8_t attempt;
    getRNG()->random(&attempt, 1);   // need this for re-tries, so packet hash (and ACK) will be different
    reply_data[len++] = (TXT_TYPE_SIGNED_PLAIN << 2) | (attempt & 3);  // 'signed' plain text

    // encode prefix of post.author.pub_key
    memcpy(&reply_data[len], post.author.pub_key, 4); len += 4;   // just first 4 bytes

    int text_len = strlen(post.text);
    memcpy(&reply_data[len], post.text, text_len); len += text_len;

    // calc expected ACK reply
    mesh::Utils::sha256((uint8_t *)&client->pending_ack, 4, reply_data, len, client->id.pub_key, PUB_KEY_SIZE);
    client->push_post_timestamp = post.post_timestamp;

    auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, client->secret, reply_data, len);
    if (reply) {
      if (client->out_path_len < 0) {
        sendFlood(reply);
        client->ack_timeout = futureMillis(PUSH_ACK_TIMEOUT_FLOOD);
      } else {
        sendDirect(reply, client->out_path, client->out_path_len);
        client->ack_timeout = futureMillis(PUSH_TIMEOUT_BASE + PUSH_ACK_TIMEOUT_FACTOR * (client->out_path_len + 1));
      }
      _num_post_pushes++;  // stats
    } else {
      client->pending_ack = 0;
      MESH_DEBUG_PRINTLN("Unable to push post to client");
    }
  }

  uint8_t getUnsyncedCount(ClientInfo* client) {
    uint8_t count = 0;
    for (int k = 0; k < MAX_UNSYNCED_POSTS; k++) {
      if (posts[k].post_timestamp > client->sync_since   // is new post for this Client?
        && !posts[k].author.matches(client->id)) {    // don't push posts to the author
        count++;
      }
    }
    return count;
  }

  bool processAck(const uint8_t *data) {
    for (int i = 0; i < num_clients; i++) {
      auto client = &known_clients[i];
      if (client->pending_ack && memcmp(data, &client->pending_ack, 4) == 0) {     // got an ACK from Client!
        client->pending_ack = 0;    // clear this, so next push can happen
        client->push_failures = 0;
        client->sync_since = client->push_post_timestamp;   // advance Client's SINCE timestamp, to sync next post
        return true;
      }
    }
    return false;
  }

  mesh::Packet* createSelfAdvert() {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE];
    uint8_t app_data_len;
    {
      AdvertDataBuilder builder(ADV_TYPE_ROOM, _prefs.node_name, _prefs.node_lat, _prefs.node_lon);
      app_data_len = builder.encodeTo(app_data);
    }

   return createAdvert(self_id, app_data, app_data_len);
  }

  File openAppend(const char* fname) {
    #if defined(NRF52_PLATFORM)
      return _fs->open(fname, FILE_O_WRITE);
    #elif defined(RP2040_PLATFORM)
      return _fs->open(fname, "a");
    #else
      return _fs->open(fname, "a", true);
    #endif
    }

  int handleRequest(ClientInfo* sender, uint32_t sender_timestamp, uint8_t* payload, size_t payload_len) {
    // uint32_t now = getRTCClock()->getCurrentTimeUnique();
    // memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
    memcpy(reply_data, &sender_timestamp, 4);   // reflect sender_timestamp back in response packet (kind of like a 'tag')

    switch (payload[0]) {
      case REQ_TYPE_GET_STATUS: {
        ServerStats stats;
        stats.batt_milli_volts = board.getBattMilliVolts();
        stats.curr_tx_queue_len = _mgr->getOutboundCount(0xFFFFFFFF);
        stats.noise_floor = (int16_t)_radio->getNoiseFloor();
        stats.last_rssi = (int16_t) radio_driver.getLastRSSI();
        stats.n_packets_recv = radio_driver.getPacketsRecv();
        stats.n_packets_sent = radio_driver.getPacketsSent();
        stats.total_air_time_secs = getTotalAirTime() / 1000;
        stats.total_up_time_secs = _ms->getMillis() / 1000;
        stats.n_sent_flood = getNumSentFlood();
        stats.n_sent_direct = getNumSentDirect();
        stats.n_recv_flood = getNumRecvFlood();
        stats.n_recv_direct = getNumRecvDirect();
        stats.err_events = _err_flags;
        stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
        stats.n_direct_dups = ((SimpleMeshTables *)getTables())->getNumDirectDups();
        stats.n_flood_dups = ((SimpleMeshTables *)getTables())->getNumFloodDups();
        stats.n_posted = _num_posted;
        stats.n_post_push = _num_post_pushes;

        memcpy(&reply_data[4], &stats, sizeof(stats));
        return 4 + sizeof(stats);
      }

      case REQ_TYPE_GET_TELEMETRY_DATA: {
        uint8_t perm_mask = ~(payload[1]);    // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions

        telemetry.reset();
        telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
        // query other sensors -- target specific
        sensors.querySensors((sender->permission == RoomPermission::ADMIN ? 0xFF : 0x00) & perm_mask, telemetry);

        uint8_t tlen = telemetry.getSize();
        memcpy(&reply_data[4], telemetry.getBuffer(), tlen);
        return 4 + tlen;  // reply_len
      }
    }
    return 0;  // unknown command
  }

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override {
    #if MESH_PACKET_LOGGING
      Serial.print(getLogDateTime());
      Serial.print(" RAW: ");
      mesh::Utils::printHex(Serial, raw, len);
      Serial.println();
    #endif
  }

  void logRx(mesh::Packet* pkt, int len, float score) override {
    if (_logging) {
      File f = openAppend(PACKET_LOG_FILE);
      if (f) {
        f.print(getLogDateTime());
        f.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d",
          len, pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
          (int)_radio->getLastSNR(), (int)_radio->getLastRSSI(), (int)(score*1000));

        if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ
          || pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
          f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
        } else {
          f.printf("\n");
        }
        f.close();
      }
    }
  }
  void logTx(mesh::Packet* pkt, int len) override {
    if (_logging) {
      File f = openAppend(PACKET_LOG_FILE);
      if (f) {
        f.print(getLogDateTime());
        f.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)",
          len, pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);

        if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ
          || pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
          f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
        } else {
          f.printf("\n");
        }
        f.close();
      }
    }
  }
  void logTxFail(mesh::Packet* pkt, int len) override {
    if (_logging) {
      File f = openAppend(PACKET_LOG_FILE);
      if (f) {
        f.print(getLogDateTime());
        f.printf(": TX FAIL!, len=%d (type=%d, route=%s, payload_len=%d)\n",
          len, pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
        f.close();
      }
    }
  }

  int calcRxDelay(float score, uint32_t air_time) const override {
    if (_prefs.rx_delay_base <= 0.0f) return 0;
    return (int) ((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
  }

  const char* getLogDateTime() override {
    static char tmp[32];
    uint32_t now = getRTCClock()->getCurrentTime();
    DateTime dt = DateTime(now);
    sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(), dt.year());
    return tmp;
  }

  uint32_t getRetransmitDelay(const mesh::Packet* packet) override {
    uint32_t t = (_radio->getEstAirtimeFor(packet->path_len + packet->payload_len + 2) * _prefs.tx_delay_factor);
    return getRNG()->nextInt(0, 6)*t;
  }
  uint32_t getDirectRetransmitDelay(const mesh::Packet* packet) override {
    uint32_t t = (_radio->getEstAirtimeFor(packet->path_len + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
    return getRNG()->nextInt(0, 6)*t;
  }
  int getInterferenceThreshold() const override {
    return _prefs.interference_threshold;
  }
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
  uint8_t getExtraAckTransmitCount() const override {
    return _prefs.multi_acks;
  }

  bool allowPacketForward(const mesh::Packet* packet) override {
    if (_prefs.disable_fwd) return false;
    if (packet->isRouteFlood() && packet->path_len >= _prefs.flood_max) return false;
    return true;
  }

  void onAnonDataRecv(mesh::Packet* packet, const uint8_t* secret, const mesh::Identity& sender, uint8_t* data, size_t len) override {
    if (packet->getPayloadType() == PAYLOAD_TYPE_ANON_REQ) {  // received an initial request by a possible admin client (unknown at this stage)
      uint32_t sender_timestamp, sender_sync_since;
      memcpy(&sender_timestamp, data, 4);
      memcpy(&sender_sync_since, &data[4], 4);  // sender's "sync messags SINCE x" timestamp

      RoomPermission perm;
      data[len] = 0;  // ensure null terminator
      if (strcmp((char *) &data[8], _prefs.password) == 0) {  // check for valid admin password
        perm = RoomPermission::ADMIN;
      } else {
        if (strcmp((char *) &data[8], _prefs.guest_password) == 0) {  // check the room/public password
          perm = RoomPermission::GUEST;
        } else if (_prefs.allow_read_only) {
          perm = RoomPermission::READ_ONLY;
        } else {
          MESH_DEBUG_PRINTLN("Incorrect room password");
          return;   // no response. Client will timeout
        }
      }

      auto client = putClient(sender);  // add to known clients (if not already known)
      if (sender_timestamp <= client->last_timestamp) {
        MESH_DEBUG_PRINTLN("possible replay attack!");
        return;
      }

      MESH_DEBUG_PRINTLN("Login success!");
      client->permission = perm;
      client->last_timestamp = sender_timestamp;
      client->sync_since = sender_sync_since;
      client->pending_ack = 0;
      client->push_failures = 0;
      memcpy(client->secret, secret, PUB_KEY_SIZE);

      uint32_t now = getRTCClock()->getCurrentTime();
      client->last_activity = now;

      now = getRTCClock()->getCurrentTimeUnique();
      memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
      // TODO: maybe reply with count of messages waiting to be synced for THIS client?
      reply_data[4] = RESP_SERVER_LOGIN_OK;
      reply_data[5] = (CLIENT_KEEP_ALIVE_SECS >> 4);  // NEW: recommended keep-alive interval (secs / 16)
      reply_data[6] = (perm == RoomPermission::ADMIN ? 1 : (perm == RoomPermission::GUEST ? 0 : 2));
      reply_data[7] = getUnsyncedCount(client);  // NEW
      memcpy(&reply_data[8], "OK", 2);  // REVISIT: not really needed

      next_push = futureMillis(PUSH_NOTIFY_DELAY_MILLIS);  // delay next push, give RESPONSE packet time to arrive first

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
        mesh::Packet* path = createPathReturn(sender, client->secret, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, reply_data, 8 + 2);
        if (path) sendFlood(path, SERVER_RESPONSE_DELAY);
      } else {
        mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, client->secret, reply_data, 8 + 2);
        if (reply) {
          if (client->out_path_len >= 0) {  // we have an out_path, so send DIRECT
            sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFlood(reply, SERVER_RESPONSE_DELAY);
          }
        }
      }
    }
  }

  int  matching_peer_indexes[MAX_CLIENTS];

  int searchPeersByHash(const uint8_t* hash) override {
    int n = 0;
    for (int i = 0; i < num_clients; i++) {
      if (known_clients[i].id.isHashMatch(hash)) {
        matching_peer_indexes[n++] = i;  // store the INDEXES of matching contacts (for subsequent 'peer' methods)
      }
    }
    return n;
  }

  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override {
    int i = matching_peer_indexes[peer_idx];
    if (i >= 0 && i < num_clients) {
      // lookup pre-calculated shared_secret
      memcpy(dest_secret, known_clients[i].secret, PUB_KEY_SIZE);
    } else {
      MESH_DEBUG_PRINTLN("getPeerSharedSecret: Invalid peer idx: %d", i);
    }
  }

  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override {
    int i = matching_peer_indexes[sender_idx];
    if (i < 0 || i >= num_clients) {  // get from our known_clients table (sender SHOULD already be known in this context)
      MESH_DEBUG_PRINTLN("onPeerDataRecv: invalid peer idx: %d", i);
      return;
    }
    auto client = &known_clients[i];
    if (type == PAYLOAD_TYPE_TXT_MSG && len > 5) {   // a CLI command or new Post
      uint32_t sender_timestamp;
      memcpy(&sender_timestamp, data, 4);  // timestamp (by sender's RTC clock - which could be wrong)
      uint flags = (data[4] >> 2);   // message attempt number, and other flags

      if (!(flags == TXT_TYPE_PLAIN || flags == TXT_TYPE_CLI_DATA)) {
        MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported command flags received: flags=%02x", (uint32_t)flags);
      } else if (sender_timestamp >= client->last_timestamp) {  // prevent replay attacks, but send Acks for retries
        bool is_retry = (sender_timestamp == client->last_timestamp);
        client->last_timestamp = sender_timestamp;

        uint32_t now = getRTCClock()->getCurrentTimeUnique();
        client->last_activity = now;
        client->push_failures = 0;  // reset so push can resume (if prev failed)

        // len can be > original length, but 'text' will be padded with zeroes
        data[len] = 0; // need to make a C string again, with null terminator

        uint32_t ack_hash;    // calc truncated hash of the message timestamp + text + sender pub_key, to prove to sender that we got it
        mesh::Utils::sha256((uint8_t *) &ack_hash, 4, data, 5 + strlen((char *)&data[5]), client->id.pub_key, PUB_KEY_SIZE);

        uint8_t temp[166];
        bool send_ack;
        if (flags == TXT_TYPE_CLI_DATA) {
          if (client->permission == RoomPermission::ADMIN) {
            if (is_retry) {
              temp[5] = 0;  // no reply
            } else {
              handleCommand(sender_timestamp, (char *) &data[5], (char *) &temp[5]);
              temp[4] = (TXT_TYPE_CLI_DATA << 2);  // attempt and flags,  (NOTE: legacy was: TXT_TYPE_PLAIN)
            }
            send_ack = false;
          } else {
            temp[5] = 0;  // no reply
            send_ack = false;  // and no ACK...  user shoudn't be sending these
          }
        } else {   // TXT_TYPE_PLAIN
          if (client->permission == RoomPermission::READ_ONLY) {
            temp[5] = 0;  // no reply
            send_ack = false;  // no ACK
          } else {
            if (!is_retry) {
              addPost(client, (const char *) &data[5]);
            }
            temp[5] = 0;  // no reply (ACK is enough)
            send_ack = true;
          }
        }

        uint32_t delay_millis;
        if (send_ack) {
          if (client->out_path_len < 0) {
            mesh::Packet* ack = createAck(ack_hash);
            if (ack) sendFlood(ack, TXT_ACK_DELAY);
            delay_millis = TXT_ACK_DELAY + REPLY_DELAY_MILLIS;
          } else {
            uint32_t d = TXT_ACK_DELAY;
            if (getExtraAckTransmitCount() > 0) {
              mesh::Packet* a1 = createMultiAck(ack_hash, 1);
              if (a1) sendDirect(a1, client->out_path, client->out_path_len, d);
              d += 300;
            }

            mesh::Packet* a2 = createAck(ack_hash);
            if (a2) sendDirect(a2, client->out_path, client->out_path_len, d);
            delay_millis = d + REPLY_DELAY_MILLIS;
          }
        } else {
          delay_millis = 0;
        }

        int text_len = strlen((char *) &temp[5]);
        if (text_len > 0) {
          if (now == sender_timestamp) {
            // WORKAROUND: the two timestamps need to be different, in the CLI view
            now++;
          }
          memcpy(temp, &now, 4);   // mostly an extra blob to help make packet_hash unique

          // calc expected ACK reply
          //mesh::Utils::sha256((uint8_t *)&expected_ack_crc, 4, temp, 5 + text_len, self_id.pub_key, PUB_KEY_SIZE);

          auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, secret, temp, 5 + text_len);
          if (reply) {
            if (client->out_path_len < 0) {
              sendFlood(reply, delay_millis + SERVER_RESPONSE_DELAY);
            } else {
              sendDirect(reply, client->out_path, client->out_path_len, delay_millis + SERVER_RESPONSE_DELAY);
            }
          }
        }
      } else {
        MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
      }
    } else if (type == PAYLOAD_TYPE_REQ && len >= 5) {
      uint32_t sender_timestamp;
      memcpy(&sender_timestamp, data, 4);  // timestamp (by sender's RTC clock - which could be wrong)
      if (sender_timestamp < client->last_timestamp) {  // prevent replay attacks
        MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
      } else {
        client->last_timestamp = sender_timestamp;

        uint32_t now = getRTCClock()->getCurrentTime();
        client->last_activity = now;   // <-- THIS will keep client connection alive
        client->push_failures = 0;  // reset so push can resume (if prev failed)

        if (data[4] == REQ_TYPE_KEEP_ALIVE && packet->isRouteDirect()) {   // request type
          uint32_t forceSince = 0;
          if (len >= 9) {   // optional - last post_timestamp client received
            memcpy(&forceSince, &data[5], 4);    // NOTE: this may be 0, if part of decrypted PADDING!
          } else {
            memcpy(&data[5], &forceSince, 4);  // make sure there are zeroes in payload (for ack_hash calc below)
          }
          if (forceSince > 0) {
            client->sync_since = forceSince;    // force-update the 'sync since'
          }

          client->pending_ack = 0;

          // TODO: Throttle KEEP_ALIVE requests!
          // if client sends too quickly, evict()

          // RULE: only send keep_alive response DIRECT!
          if (client->out_path_len >= 0) {
            uint32_t ack_hash;    // calc ACK to prove to sender that we got request
            mesh::Utils::sha256((uint8_t *) &ack_hash, 4, data, 9, client->id.pub_key, PUB_KEY_SIZE);

            auto reply = createAck(ack_hash);
            if (reply) {
              reply->payload[reply->payload_len++] = getUnsyncedCount(client);  // NEW: add unsynced counter to end of ACK packet
              sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
            }
          }
        } else {
          int reply_len = handleRequest(client, sender_timestamp, &data[4], len - 4);
          if (reply_len > 0) {  // valid command
            if (packet->isRouteFlood()) {
              // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
              mesh::Packet* path = createPathReturn(client->id, secret, packet->path, packet->path_len,
                                                    PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
              if (path) sendFlood(path, SERVER_RESPONSE_DELAY);
            } else {
              mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
              if (reply) {
                if (client->out_path_len >= 0) {  // we have an out_path, so send DIRECT
                  sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
                } else {
                  sendFlood(reply, SERVER_RESPONSE_DELAY);
                }
              }
            }
          }
        }
      }
    }
  }

  bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override {
    // TODO: prevent replay attacks
    int i = matching_peer_indexes[sender_idx];

    if (i >= 0 && i < num_clients) {  // get from our known_clients table (sender SHOULD already be known in this context)
      MESH_DEBUG_PRINTLN("PATH to client, path_len=%d", (uint32_t) path_len);
      auto client = &known_clients[i];
      memcpy(client->out_path, path, client->out_path_len = path_len);  // store a copy of path, for sendDirect()
    } else {
      MESH_DEBUG_PRINTLN("onPeerPathRecv: invalid peer idx: %d", i);
    }

    if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4) {
      // also got an encoded ACK!
      processAck(extra);
    }

    // NOTE: no reciprocal path send!!
    return false;
  }

  void onAckRecv(mesh::Packet* packet, uint32_t ack_crc) override {
    if (processAck((uint8_t *)&ack_crc)) {
      packet->markDoNotRetransmit();   // ACK was for this node, so don't retransmit
    }
  }

public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
     : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), tables),
      _cli(board, rtc, &_prefs, this), telemetry(MAX_PACKET_PAYLOAD - 4)
  {
    next_local_advert = next_flood_advert = 0;
    _logging = false;
    set_radio_at = revert_radio_at = 0;

    // defaults
    memset(&_prefs, 0, sizeof(_prefs));
    _prefs.airtime_factor = 1.0;    // one half
    _prefs.rx_delay_base = 0.0f;   // off by default, was 10.0
    _prefs.tx_delay_factor = 0.5f;   // was 0.25f;
    StrHelper::strncpy(_prefs.node_name, ADVERT_NAME, sizeof(_prefs.node_name));
    _prefs.node_lat = ADVERT_LAT;
    _prefs.node_lon = ADVERT_LON;
    StrHelper::strncpy(_prefs.password, ADMIN_PASSWORD, sizeof(_prefs.password));
    _prefs.freq = LORA_FREQ;
    _prefs.sf = LORA_SF;
    _prefs.bw = LORA_BW;
    _prefs.cr = LORA_CR;
    _prefs.tx_power_dbm = LORA_TX_POWER;
    _prefs.disable_fwd = 1;
    _prefs.advert_interval = 1;  // default to 2 minutes for NEW installs
    _prefs.flood_advert_interval = 12;   // 12 hours
    _prefs.flood_max = 64;
    _prefs.interference_threshold = 0;  // disabled
  #ifdef ROOM_PASSWORD
    StrHelper::strncpy(_prefs.guest_password, ROOM_PASSWORD, sizeof(_prefs.guest_password));
  #endif

    num_clients = 0;
    next_post_idx = 0;
    next_client_idx = 0;
    next_push = 0;
    memset(posts, 0, sizeof(posts));
    _num_posted = _num_post_pushes = 0;
  }

  void begin(FILESYSTEM* fs) {
    mesh::Mesh::begin();
    _fs = fs;
    // load persisted prefs
    _cli.loadPrefs(_fs);

    radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    radio_set_tx_power(_prefs.tx_power_dbm);

    updateAdvertTimer();
    updateFloodAdvertTimer();
  }

  const char* getFirmwareVer() override { return FIRMWARE_VERSION; }
  const char* getBuildDate() override { return FIRMWARE_BUILD_DATE; }
  const char* getRole() override { return FIRMWARE_ROLE; }
  const char* getNodeName() { return _prefs.node_name; }
  NodePrefs* getNodePrefs() {
    return &_prefs;
  }

  void savePrefs() override {
    _cli.savePrefs(_fs);
  }

  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) override {
    set_radio_at = futureMillis(2000);   // give CLI reply some time to be sent back, before applying temp radio params
    pending_freq = freq;
    pending_bw = bw;
    pending_sf = sf;
    pending_cr = cr;

    revert_radio_at = futureMillis(2000 + timeout_mins*60*1000);   // schedule when to revert radio params
  }

  bool formatFileSystem() override {
    #if defined(NRF52_PLATFORM)
      return InternalFS.format();
    #elif defined(RP2040_PLATFORM)
      return LittleFS.format();
    #elif defined(ESP32)
      return SPIFFS.format();
    #else
      #error "need to implement file system erase"
      return false;
    #endif
  }

  void sendSelfAdvertisement(int delay_millis) override {
    mesh::Packet* pkt = createSelfAdvert();
    if (pkt) {
      sendFlood(pkt, delay_millis);
    } else {
      MESH_DEBUG_PRINTLN("ERROR: unable to create advertisement packet!");
    }
  }

  void updateAdvertTimer() override {
    if (_prefs.advert_interval > 0) {  // schedule local advert timer
      next_local_advert = futureMillis((uint32_t)_prefs.advert_interval * 2 * 60 * 1000);
    } else {
      next_local_advert = 0;  // stop the timer
    }
  }
  void updateFloodAdvertTimer() override {
    if (_prefs.flood_advert_interval > 0) {  // schedule flood advert timer
      next_flood_advert = futureMillis( ((uint32_t)_prefs.flood_advert_interval) * 60 * 60 * 1000);
    } else {
      next_flood_advert = 0;  // stop the timer
    }
  }

  void setLoggingOn(bool enable) override { _logging = enable; }

  void eraseLogFile() override {
    _fs->remove(PACKET_LOG_FILE);
  }

  void dumpLogFile() override {
  #if defined(RP2040_PLATFORM)
    File f = _fs->open(PACKET_LOG_FILE, "r");
  #else
    File f = _fs->open(PACKET_LOG_FILE);
  #endif
    if (f) {
      while (f.available()) {
        int c = f.read();
        if (c < 0) break;
        Serial.print((char)c);
      }
      f.close();
    }
  }

  void setTxPower(uint8_t power_dbm) override {
    radio_set_tx_power(power_dbm);
  }

  void formatNeighborsReply(char *reply) override {
    strcpy(reply, "not supported");
  }

  mesh::LocalIdentity& getSelfId() override { return self_id; }

  void saveIdentity(const mesh::LocalIdentity& new_id) override {
    self_id = new_id;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    IdentityStore store(*_fs, "");
#elif defined(ESP32)
    IdentityStore store(*_fs, "/identity");
#elif defined(RP2040_PLATFORM)
    IdentityStore store(*_fs, "/identity");
#else
    #error "need to define saveIdentity()"
#endif
    store.save("_main", self_id);
  }

  void clearStats() override {
    radio_driver.resetStats();
    resetStats();
    ((SimpleMeshTables *)getTables())->resetStats();
  }

  void handleCommand(uint32_t sender_timestamp, char* command, char* reply) {
    while (*command == ' ') command++;   // skip leading spaces

    if (strlen(command) > 4 && command[2] == '|') {  // optional prefix (for companion radio CLI)
      memcpy(reply, command, 3);  // reflect the prefix back
      reply += 3;
      command += 3;
    }

    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }

  void loop() {
    mesh::Mesh::loop();

    if (millisHasNowPassed(next_push) && num_clients > 0) {
      // check for ACK timeouts
      for (int i = 0; i < num_clients; i++) {
        auto c = &known_clients[i];
        if (c->pending_ack && millisHasNowPassed(c->ack_timeout)) {
          c->push_failures++;
          c->pending_ack = 0;   // reset  (TODO: keep prev expected_ack's in a list, incase they arrive LATER, after we retry)
          MESH_DEBUG_PRINTLN("pending ACK timed out: push_failures: %d", (uint32_t)c->push_failures);
        }
      }
      // check next Round-Robin client, and sync next new post
      auto client = &known_clients[next_client_idx];
      bool did_push = false;
      if (client->pending_ack == 0 && client->last_activity != 0 && client->push_failures < 3) {  // not already waiting for ACK, AND not evicted, AND retries not max
        MESH_DEBUG_PRINTLN("loop - checking for client %02X", (uint32_t) client->id.pub_key[0]);
        uint32_t now = getRTCClock()->getCurrentTime();
        for (int k = 0, idx = next_post_idx; k < MAX_UNSYNCED_POSTS; k++) {
          auto p = &posts[idx];
          if (now >= p->post_timestamp + POST_SYNC_DELAY_SECS && p->post_timestamp > client->sync_since   // is new post for this Client?
            && !p->author.matches(client->id)) {    // don't push posts to the author
            // push this post to Client, then wait for ACK
            pushPostToClient(client, *p);
            did_push = true;
            MESH_DEBUG_PRINTLN("loop - pushed to client %02X: %s", (uint32_t) client->id.pub_key[0], p->text);
            break;
          }
          idx = (idx + 1) % MAX_UNSYNCED_POSTS;   // wrap to start of cyclic queue
        }
      } else {
        MESH_DEBUG_PRINTLN("loop - skipping busy (or evicted) client %02X", (uint32_t) client->id.pub_key[0]);
      }
      next_client_idx = (next_client_idx + 1) % num_clients;  // round robin polling for each client

      if (did_push) {
        next_push = futureMillis(SYNC_PUSH_INTERVAL);
      } else {
        // were no unsynced posts for curr client, so proccess next client much quicker! (in next loop())
        next_push = futureMillis(SYNC_PUSH_INTERVAL / 8);
      }
    }

    if (next_flood_advert && millisHasNowPassed(next_flood_advert)) {
      mesh::Packet* pkt = createSelfAdvert();
      if (pkt) sendFlood(pkt);

      updateFloodAdvertTimer();   // schedule next flood advert
      updateAdvertTimer();   // also schedule local advert (so they don't overlap)
    } else if (next_local_advert && millisHasNowPassed(next_local_advert)) {
      mesh::Packet* pkt = createSelfAdvert();
      if (pkt) sendZeroHop(pkt);

      updateAdvertTimer();   // schedule next local advert
    }

    if (set_radio_at && millisHasNowPassed(set_radio_at)) {   // apply pending (temporary) radio params
      set_radio_at = 0;  // clear timer
      radio_set_params(pending_freq, pending_bw, pending_sf, pending_cr);
      MESH_DEBUG_PRINTLN("Temp radio params");
    }

    if (revert_radio_at && millisHasNowPassed(revert_radio_at)) {   // revert radio params to orig
      revert_radio_at = 0;  // clear timer
      radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
      MESH_DEBUG_PRINTLN("Radio params restored");
    }

  #ifdef DISPLAY_CLASS
    ui_task.loop();
  #endif

    // TODO: periodically check for OLD/inactive entries in known_clients[], and evict
  }
};

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[MAX_POST_TEXT_LEN+1];

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Room ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial Advertisement to the mesh
  the_mesh.sendSelfAdvertisement(16000);
}

void loop() {
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
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  the_mesh.loop();
  sensors.loop();
}
