#include "MyMesh.h"
#include <algorithm>
#include <stdlib.h>  // for qsort()
#include <helpers/RxReservePacketManager.h>
#if defined(WITH_MQTT_NEIGHBORS)
#include <helpers/MQTTConnectionPolicy.h>  // kSyncedClockEpoch
#endif

/* ------------------------------ Config -------------------------------- */

#ifndef LORA_FREQ
  #define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
  #define LORA_BW 250
#endif
#ifndef LORA_SF
  #define LORA_SF 10
#endif
#ifndef LORA_CR
  #define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER 20
#endif

#ifndef ADVERT_NAME
  #define ADVERT_NAME "repeater"
#endif
#ifndef ADVERT_LAT
  #define ADVERT_LAT 0.0
#endif
#ifndef ADVERT_LON
  #define ADVERT_LON 0.0
#endif

#ifndef ADMIN_PASSWORD
  #define ADMIN_PASSWORD "password"
#endif

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY 300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY 200
#endif

#define FIRMWARE_VER_LEVEL       2

#define REQ_TYPE_GET_STATUS         0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE         0x02
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#define REQ_TYPE_GET_ACCESS_LIST    0x05
#define REQ_TYPE_GET_NEIGHBOURS     0x06
#define REQ_TYPE_GET_OWNER_INFO     0x07     // FIRMWARE_VER_LEVEL >= 2

#define RESP_SERVER_LOGIN_OK        0 // response to ANON_REQ

#define ANON_REQ_TYPE_REGIONS      0x01
#define ANON_REQ_TYPE_OWNER        0x02
#define ANON_REQ_TYPE_BASIC        0x03   // just remote clock

#define CLI_REPLY_DELAY_MILLIS      600

// Max time to flush the outbound queue (START alert + CLI reply) before the OTA
// teardown blocks the loop until reboot. Best-effort: exits early once the queue
// drains (immediate on a healthy node), caps the wait on a jammed/duty-limited
// channel so an update is never stalled indefinitely.
#define OTA_TX_DRAIN_TIMEOUT_MS     5000

#define LAZY_CONTACTS_WRITE_DELAY    5000

void MyMesh::putNeighbour(const mesh::Identity &id, uint32_t timestamp, float snr) {
#if MAX_NEIGHBOURS // check if neighbours enabled
  // find existing neighbour, else use least recently updated
  uint32_t oldest_timestamp = 0xFFFFFFFF;
  NeighbourInfo *neighbour = &neighbours[0];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    // if neighbour already known, we should update it
    if (id.matches(neighbours[i].id)) {
      neighbour = &neighbours[i];
      break;
    }

    // otherwise we should update the least recently updated neighbour
    if (neighbours[i].heard_timestamp < oldest_timestamp) {
      neighbour = &neighbours[i];
      oldest_timestamp = neighbour->heard_timestamp;
    }
  }

  // update neighbour info
  neighbour->id = id;
  neighbour->advert_timestamp = timestamp;
  neighbour->heard_timestamp = getRTCClock()->getCurrentTime();
  neighbour->snr = (int8_t)(snr * 4);
#endif
}

uint8_t MyMesh::handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood) {
  ClientInfo* client = NULL;
  if (data[0] == 0) {   // blank password, just check if sender is in ACL
    client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    if (client == NULL) {
    #if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Login, sender not in ACL");
    #endif
    }
  }
  if (client == NULL) {
    uint8_t perms;
    if (strcmp((char *)data, _prefs.password) == 0) { // check for valid admin password
      perms = PERM_ACL_ADMIN;
    } else if (strcmp((char *)data, _prefs.guest_password) == 0) { // check guest password
      perms = PERM_ACL_GUEST;
    } else {
#if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Invalid password: %s", data);
#endif
      return 0;
    }

    client = acl.putClient(sender, 0);  // add to contacts (if not already known)
    if (sender_timestamp <= client->last_timestamp) {
      MESH_DEBUG_PRINTLN("Possible login replay attack!");
      return 0;  // FATAL: client table is full -OR- replay attack
    }

    MESH_DEBUG_PRINTLN("Login success!");
    client->last_timestamp = sender_timestamp;
    client->last_activity = getRTCClock()->getCurrentTime();
    client->permissions &= ~0x03;
    client->permissions |= perms;
    memcpy(client->shared_secret, secret, PUB_KEY_SIZE);

    if (perms != PERM_ACL_GUEST) {   // keep number of FS writes to a minimum
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    }
  }

  if (is_flood) {
    client->out_path_len = OUT_PATH_UNKNOWN;  // need to rediscover out_path
  }

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  reply_data[4] = RESP_SERVER_LOGIN_OK;
  reply_data[5] = 0;  // Legacy: was recommended keep-alive interval (secs / 16)
  reply_data[6] = client->isAdmin() ? 1 : 0;
  reply_data[7] = client->permissions;
  getRNG()->random(&reply_data[8], 4);   // random blob to help packet-hash uniqueness
  reply_data[12] = FIRMWARE_VER_LEVEL;  // New field

  return 13;  // reply length
}

// Comparison functions for qsort() - defined at file scope to avoid heap allocations
static int cmp_neighbours_newest_to_oldest(const void* a, const void* b) {
  const NeighbourInfo* na = *(const NeighbourInfo**)a;
  const NeighbourInfo* nb = *(const NeighbourInfo**)b;
  if (nb->heard_timestamp > na->heard_timestamp) return 1;
  if (nb->heard_timestamp < na->heard_timestamp) return -1;
  return 0;
}

static int cmp_neighbours_oldest_to_newest(const void* a, const void* b) {
  const NeighbourInfo* na = *(const NeighbourInfo**)a;
  const NeighbourInfo* nb = *(const NeighbourInfo**)b;
  if (na->heard_timestamp > nb->heard_timestamp) return 1;
  if (na->heard_timestamp < nb->heard_timestamp) return -1;
  return 0;
}

static int cmp_neighbours_strongest_to_weakest(const void* a, const void* b) {
  const NeighbourInfo* na = *(const NeighbourInfo**)a;
  const NeighbourInfo* nb = *(const NeighbourInfo**)b;
  if (nb->snr > na->snr) return 1;
  if (nb->snr < na->snr) return -1;
  return 0;
}

static int cmp_neighbours_weakest_to_strongest(const void* a, const void* b) {
  const NeighbourInfo* na = *(const NeighbourInfo**)a;
  const NeighbourInfo* nb = *(const NeighbourInfo**)b;
  if (na->snr > nb->snr) return 1;
  if (na->snr < nb->snr) return -1;
  return 0;
}

uint8_t MyMesh::handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)

    return 8 + region_map.exportNamesTo((char *) &reply_data[8], sizeof(reply_data) - 12, REGION_DENY_FLOOD);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    sprintf((char *) &reply_data[8], "%s\n%s", _prefs.node_name, _prefs.owner_info);

    return 8 + strlen((char *) &reply_data[8]);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    reply_data[8] = 0;  // features
#ifdef WITH_RS232_BRIDGE
    reply_data[8] |= 0x01;  // is bridge, type UART
#elif WITH_ESPNOW_BRIDGE
    reply_data[8] |= 0x03;  // is bridge, type ESP-NOW
#endif
    if (_prefs.disable_fwd) {   // is this repeater currently disabled
      reply_data[8] |= 0x80;  // is disabled
    }
    // TODO:  add some kind of moving-window utilisation metric, so can query 'how busy' is this repeater
    return 9;   // reply length
  }
  return 0;
}

int MyMesh::handleRequest(ClientInfo *sender, uint32_t sender_timestamp, uint8_t *payload, size_t payload_len) {
  // uint32_t now = getRTCClock()->getCurrentTimeUnique();
  // memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  memcpy(reply_data, &sender_timestamp, 4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

  if (payload[0] == REQ_TYPE_GET_STATUS) {  // guests can also access this now
    RepeaterStats stats;
    stats.batt_milli_volts = board.getBattMilliVolts();
    stats.curr_tx_queue_len = _mgr->getOutboundCount(0xFFFFFFFF);
    stats.noise_floor = (int16_t)_radio->getNoiseFloor();
    stats.last_rssi = (int16_t)radio_driver.getLastRSSI();
    stats.n_packets_recv = radio_driver.getPacketsRecv();
    stats.n_packets_sent = radio_driver.getPacketsSent();
    stats.total_air_time_secs = getTotalAirTime() / 1000;
    stats.total_up_time_secs = uptime_millis / 1000;
    stats.n_sent_flood = getNumSentFlood();
    stats.n_sent_direct = getNumSentDirect();
    stats.n_recv_flood = getNumRecvFlood();
    stats.n_recv_direct = getNumRecvDirect();
    stats.err_events = _err_flags;
    stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
    stats.n_direct_dups = ((SimpleMeshTables *)getTables())->getNumDirectDups();
    stats.n_flood_dups = ((SimpleMeshTables *)getTables())->getNumFloodDups();
    stats.total_rx_air_time_secs = getReceiveAirTime() / 1000;
    stats.n_recv_errors = radio_driver.getPacketsRecvErrors();
    memcpy(&reply_data[4], &stats, sizeof(stats));

    return 4 + sizeof(stats); //  reply_len
  }
  if (payload[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t perm_mask = ~(payload[1]); // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions

    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);

    // query other sensors -- target specific
    if ((sender->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {
      perm_mask = 0x00;  // just base telemetry allowed
    }
    sensors.querySensors(perm_mask, telemetry);

	// This default temperature will be overridden by external sensors (if any)
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    uint8_t tlen = telemetry.getSize();
    memcpy(&reply_data[4], telemetry.getBuffer(), tlen);
    return 4 + tlen; // reply_len
  }
  if (payload[0] == REQ_TYPE_GET_ACCESS_LIST && sender->isAdmin()) {
    uint8_t res1 = payload[1];   // reserved for future  (extra query params)
    uint8_t res2 = payload[2];
    if (res1 == 0 && res2 == 0) {
      uint8_t ofs = 4;
      for (int i = 0; i < acl.getNumClients() && ofs + 7 <= sizeof(reply_data) - 4; i++) {
        auto c = acl.getClientByIdx(i);
        if (c->permissions == 0) continue;  // skip deleted entries
        memcpy(&reply_data[ofs], c->id.pub_key, 6); ofs += 6;  // just 6-byte pub_key prefix
        reply_data[ofs++] = c->permissions;
      }
      return ofs;
    }
  }
  if (payload[0] == REQ_TYPE_GET_NEIGHBOURS) {
    uint8_t request_version = payload[1];
    if (request_version == 0) {

      // reply data offset (after response sender_timestamp/tag)
      int reply_offset = 4;

      // get request params
      uint8_t count = payload[2]; // how many neighbours to fetch (0-255)
      uint16_t offset;
      memcpy(&offset, &payload[3], 2); // offset from start of neighbours list (0-65535)
      uint8_t order_by = payload[5]; // how to order neighbours. 0=newest_to_oldest, 1=oldest_to_newest, 2=strongest_to_weakest, 3=weakest_to_strongest
      uint8_t pubkey_prefix_length = payload[6]; // how many bytes of neighbour pub key we want
      // we also send a 4 byte random blob in payload[7...10] to help packet uniqueness

      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS count=%d, offset=%d, order_by=%d, pubkey_prefix_length=%d", count, offset, order_by, pubkey_prefix_length);

      // clamp pub key prefix length to max pub key length
      if(pubkey_prefix_length > PUB_KEY_SIZE){
        pubkey_prefix_length = PUB_KEY_SIZE;
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS invalid pubkey_prefix_length=%d clamping to %d", pubkey_prefix_length, PUB_KEY_SIZE);
      }

      // Early exit if no neighbours to avoid unnecessary processing
      int16_t neighbours_count = 0;
#if MAX_NEIGHBOURS
      NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
#endif
      for (int i = 0; i < MAX_NEIGHBOURS; i++) {
        if (neighbours[i].heard_timestamp > 0) {
          neighbours_count++;
        }
      }
      
      if (neighbours_count == 0) {
        // No neighbours - return minimal response
        memcpy(&reply_data[reply_offset], &neighbours_count, 2); reply_offset += 2;
        uint16_t zero = 0;
        memcpy(&reply_data[reply_offset], &zero, 2); reply_offset += 2; // results_count = 0
        return reply_offset;
      }

      // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
      int16_t sorted_idx = 0;
      for (int i = 0; i < MAX_NEIGHBOURS; i++) {
        auto neighbour = &neighbours[i];
        if (neighbour->heard_timestamp > 0) {
          sorted_neighbours[sorted_idx++] = neighbour;
        }
      }

      // Sort neighbours based on order using qsort() - standard C library function
      // qsort() doesn't allocate heap memory (uses stack-based recursion) and is O(n log n)
      // This matches the pattern used elsewhere in the codebase (e.g., BaseChatMesh)
      if (order_by == 0) {
        // sort by newest to oldest
        qsort(sorted_neighbours, neighbours_count, sizeof(NeighbourInfo*), cmp_neighbours_newest_to_oldest);
      } else if (order_by == 1) {
        // sort by oldest to newest
        qsort(sorted_neighbours, neighbours_count, sizeof(NeighbourInfo*), cmp_neighbours_oldest_to_newest);
      } else if (order_by == 2) {
        // sort by strongest to weakest
        qsort(sorted_neighbours, neighbours_count, sizeof(NeighbourInfo*), cmp_neighbours_strongest_to_weakest);
      } else if (order_by == 3) {
        // sort by weakest to strongest
        qsort(sorted_neighbours, neighbours_count, sizeof(NeighbourInfo*), cmp_neighbours_weakest_to_strongest);
      }

      // build results buffer
      int results_count = 0;
      int results_offset = 0;
      uint8_t results_buffer[130];
      for(int index = 0; index < count && index + offset < neighbours_count; index++){
        
        // stop if we can't fit another entry in results
        int entry_size = pubkey_prefix_length + 4 + 1;
        if(results_offset + entry_size > sizeof(results_buffer)){
          MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS no more entries can fit in results buffer");
          break;
        }

#if MAX_NEIGHBOURS
        // add next neighbour to results
        auto neighbour = sorted_neighbours[index + offset];
        uint32_t heard_seconds_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
        memcpy(&results_buffer[results_offset], neighbour->id.pub_key, pubkey_prefix_length); results_offset += pubkey_prefix_length;
        memcpy(&results_buffer[results_offset], &heard_seconds_ago, 4); results_offset += 4;
        memcpy(&results_buffer[results_offset], &neighbour->snr, 1); results_offset += 1;
        results_count++;
#endif

      }

      // build reply
      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS neighbours_count=%d results_count=%d", neighbours_count, results_count);
      memcpy(&reply_data[reply_offset], &neighbours_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_buffer, results_offset); reply_offset += results_offset;

      return reply_offset;
    }
  } else if (payload[0] == REQ_TYPE_GET_OWNER_INFO) {
    sprintf((char *) &reply_data[4], "%s\n%s\n%s", FIRMWARE_VERSION, _prefs.node_name, _prefs.owner_info);
    return 4 + strlen((char *) &reply_data[4]);
  }
  return 0; // unknown command
}

mesh::Packet *MyMesh::createSelfAdvert() {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len = _cli.buildAdvertData(ADV_TYPE_REPEATER, app_data);

  return createAdvert(self_id, app_data, app_data_len);
}

File MyMesh::openAppend(const char *fname) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(fname, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return _fs->open(fname, "a");
#else
  return _fs->open(fname, "a", true);
#endif
}

static uint8_t max_loop_minimal[] =  { 0, /* 1-byte */  4, /* 2-byte */  2, /* 3-byte */  1 };
static uint8_t max_loop_moderate[] = { 0, /* 1-byte */  2, /* 2-byte */  1, /* 3-byte */  1 };
static uint8_t max_loop_strict[] =   { 0, /* 1-byte */  1, /* 2-byte */  1, /* 3-byte */  1 };

bool MyMesh::isLooped(const mesh::Packet* packet, const uint8_t max_counters[]) {
  uint8_t hash_size = packet->getPathHashSize();
  uint8_t hash_count = packet->getPathHashCount();
  uint8_t n = 0;
  const uint8_t* path = packet->path;
  while (hash_count > 0) {      // count how many times this node is already in the path
    if (self_id.isHashMatch(path, hash_size)) n++;
    hash_count--;
    path += hash_size;
  }
  return n >= max_counters[hash_size];
}

void MyMesh::sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size) {
  TransportKey req_scope;
  bool is_wildcard = recv_pkt_region != NULL && recv_pkt_region->isWildcard();
  bool req_scope_known = recv_pkt_region != NULL && !is_wildcard
                      && region_map.getTransportKeysFor(*recv_pkt_region, &req_scope, 1) > 0;

  switch (mesh::chooseReplyScope(req_scope_known, is_wildcard, !default_scope.isNull())) {
    case mesh::REPLY_SCOPE_REQUEST:
      sendFloodScoped(req_scope, packet, delay_millis, path_hash_size);   // reply with same scope as request
      break;
    case mesh::REPLY_SCOPE_DEFAULT:
      // requester's scope is unknown: DIRECT request (no transport codes), or code matched no Region.
      // un-scoped would be dropped at hop 0 by repeaters running flood.max.unscoped=0
      sendFloodScoped(default_scope, packet, delay_millis, path_hash_size);
      break;
    case mesh::REPLY_SCOPE_NONE:
      sendFlood(packet, delay_millis, path_hash_size);  // send un-scoped
      break;
  }
}

bool MyMesh::allowPacketForward(const mesh::Packet *packet) {
  if (_prefs.disable_fwd) return false;
  if (packet->isRouteFlood()
      && mesh::isFloodHopLimitExceeded(packet, _prefs.flood_max, _prefs.flood_max_unscoped, _prefs.flood_max_advert)) {
    return false;
  }
  if (packet->isRouteFlood() && recv_pkt_region == NULL) {
    MESH_DEBUG_PRINTLN("allowPacketForward: unknown transport code, or wildcard not allowed for FLOOD packet");
    return false;
  }
  if (packet->isRouteFlood() && _prefs.loop_detect != LOOP_DETECT_OFF) {
    const uint8_t* maximums;
    if (_prefs.loop_detect == LOOP_DETECT_MINIMAL) {
      maximums = max_loop_minimal;
    } else if (_prefs.loop_detect == LOOP_DETECT_MODERATE) {
      maximums = max_loop_moderate;
    } else {
      maximums = max_loop_strict;
    }
    if (isLooped(packet, maximums)) {
      MESH_DEBUG_PRINTLN("allowPacketForward: FLOOD packet loop detected!");
      return false;
    }
  }
  return true;
}

const char *MyMesh::getLogDateTime() {
  static char tmp[32];
  uint32_t now = getRTCClock()->getCurrentTime();
  DateTime dt = DateTime(now);
  sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(),
          dt.year());
  return tmp;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
#if MESH_PACKET_LOGGING
  if (Serial.availableForWrite() > 0) {
    Serial.print(getLogDateTime());
    Serial.print(" RAW: ");
    mesh::Utils::printHex(Serial, raw, len);
    Serial.println();
  }
#endif

#ifdef WITH_BRIDGE
  if (_prefs.bridge_enabled) {
    // Store raw radio data for MQTT messages
    if (bridge) bridge->storeRawRadioData(raw, len, snr, rssi);
  }
#endif
}

void MyMesh::logRx(mesh::Packet *pkt, int len, float score) {
#ifdef WITH_MQTT_BRIDGE
  // MQTT bridge: always feed RX packets — bridge decides based on mqtt.rx setting
  if (bridge) bridge->onPacketReceived(pkt);
#elif defined(WITH_BRIDGE)
  // Non-MQTT bridge (ESP-NOW): use bridge.source setting
  if (_prefs.bridge_pkt_src == 1) {
    if (bridge) bridge->onPacketReceived(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d", len,
               pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
               (int)_radio->getLastSNR(), (int)_radio->getLastRSSI(), (int)(score * 1000));

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTx(mesh::Packet *pkt, int len) {
#if defined(WITH_MQTT_NEIGHBORS)
  if (neighbor_discover_active && pkt == neighbor_discover_request
      && neighbor_discover_next < neighbor_discover_count) {
    NeighborDiscoverEntry& entry = neighbor_discover[neighbor_discover_next];
    if (entry.status == ND_QUEUED) {
      entry.status = ND_PENDING;
      neighbor_discover_queried_count++;
      neighbor_discover_request = NULL;
      neighbor_discover_until = futureMillis(neighborDiscoverQueryTimeoutMs());
    }
  }
#endif

#ifdef WITH_MQTT_BRIDGE
  // MQTT bridge: always feed TX packets — bridge decides based on mqtt.tx setting
  if (bridge) bridge->sendPacket(pkt);
#elif defined(WITH_BRIDGE)
  // Non-MQTT bridge (ESP-NOW): use bridge.source setting
  if (_prefs.bridge_pkt_src == 0) {
    if (bridge) bridge->sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTxFail(mesh::Packet *pkt, int len) {
#if defined(WITH_MQTT_NEIGHBORS)
  if (neighbor_discover_active && pkt == neighbor_discover_request
      && neighbor_discover_next < neighbor_discover_count) {
    NeighborDiscoverEntry& entry = neighbor_discover[neighbor_discover_next];
    if (entry.status == ND_QUEUED) {
      entry.status = ND_SEND_FAILED;
      neighbor_discover_request = NULL;
      neighbor_discover_until = 0;
    }
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX FAIL!, len=%d (type=%d, route=%s, payload_len=%d)\n", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
      f.close();
    }
  }
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}

mesh::DispatcherAction MyMesh::onRecvPacket(mesh::Packet* pkt) {
  if (pkt->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) {
    recv_pkt_region = region_map.findMatch(pkt, REGION_DENY_FLOOD);
  } else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) {
    if (region_map.getWildcard().flags & REGION_DENY_FLOOD) {
      recv_pkt_region = NULL;
    } else {
      recv_pkt_region =  &region_map.getWildcard();
    }
  } else {
    recv_pkt_region = NULL;
  }
  return Mesh::onRecvPacket(pkt);
}

void MyMesh::onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret, const mesh::Identity &sender,
                            uint8_t *data, size_t len) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_ANON_REQ) { // received an initial request by a possible admin
                                                           // client (unknown at this stage)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    data[len] = 0;  // ensure null terminator
    uint8_t reply_len;

    reply_path_len = 0xFF;
    if (data[4] == 0 || data[4] >= ' ') {   // is password, ie. a login request
      reply_len = handleLoginReq(sender, secret, timestamp, &data[4], packet->isRouteFlood());
    } else if (data[4] == ANON_REQ_TYPE_REGIONS && packet->isRouteDirect()) {
      reply_len = handleAnonRegionsReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_OWNER && packet->isRouteDirect()) {
      reply_len = handleAnonOwnerReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_BASIC && packet->isRouteDirect()) {
      reply_len = handleAnonClockReq(sender, timestamp, &data[5]);
    } else {
      reply_len = 0;  // unknown/invalid request type
    }

    if (reply_len == 0) return;   // invalid request

    // a DIRECT login can reply via the stored out_path, as onPeerDataRecv() does for REQ
    ClientInfo* client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    bool have_out_path = client != NULL && client->out_path_len != OUT_PATH_UNKNOWN;

    auto route = mesh::chooseReplyRoute(packet->isRouteFlood(), reply_path_len != 0xFF, have_out_path);

    if (route == mesh::REPLY_ROUTE_PATH_RETURN) {
      // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
      mesh::Packet* path = createPathReturn(sender, secret, packet->path, packet->path_len,
                                            PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
      if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      return;
    }

    mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, secret, reply_data, reply_len);
    if (reply == NULL) return;

    if (route == mesh::REPLY_ROUTE_DIRECT_SUPPLIED) {
      sendDirect(reply, reply_path, reply_path_len, SERVER_RESPONSE_DELAY);
    } else if (route == mesh::REPLY_ROUTE_DIRECT_OUT_PATH) {
      sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
    } else {
      sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    }
  }
}

int MyMesh::searchPeersByHash(const uint8_t *hash) {
  int n = 0;
#if defined(WITH_MQTT_NEIGHBORS)
  // While a neighbor-scope discovery is active, overlay the heard neighbours
  // that are NOT already ACL clients so their RESPONSE packets can be decoded.
  // Overlay indices are offset by NEIGHBOR_DISCOVER_PEER_BASE to keep them
  // distinct from real ACL indices.
  if (neighbor_discover_active) {
    for (int i = 0; i < neighbor_discover_count && n < MAX_CLIENTS; i++) {
      auto& entry = neighbor_discover[i];
      if (acl.getClient(entry.id.pub_key, PUB_KEY_SIZE) != nullptr) continue;
      if (entry.heard_timestamp > 0 && entry.id.isHashMatch(hash)) {
        matching_peer_indexes[n++] = NEIGHBOR_DISCOVER_PEER_BASE + i;
      }
    }
  }
#endif
  for (int i = 0; i < acl.getNumClients() && n < MAX_CLIENTS; i++) {
    if (acl.getClientByIdx(i)->id.isHashMatch(hash)) {
      matching_peer_indexes[n++] = i; // store the INDEXES of matching contacts (for subsequent 'peer' methods)
    }
  }
  return n;
}

void MyMesh::getPeerSharedSecret(uint8_t *dest_secret, int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
#if defined(WITH_MQTT_NEIGHBORS)
  // Overlay entries have no precomputed shared secret; derive it on the fly.
  if (neighbor_discover_active && i >= NEIGHBOR_DISCOVER_PEER_BASE) {
    int oi = i - NEIGHBOR_DISCOVER_PEER_BASE;
    if (oi >= 0 && oi < neighbor_discover_count) {
      self_id.calcSharedSecret(dest_secret, neighbor_discover[oi].id);
      return;
    }
  }
#endif
  if (i >= 0 && i < acl.getNumClients()) {
    // lookup pre-calculated shared_secret
    memcpy(dest_secret, acl.getClientByIdx(i)->shared_secret, PUB_KEY_SIZE);
  } else {
    MESH_DEBUG_PRINTLN("getPeerSharedSecret: Invalid peer idx: %d", i);
  }
}

static bool isShare(const mesh::Packet *packet) {
  if (packet->hasTransportCodes()) {
    return packet->transport_codes[0] == 0 && packet->transport_codes[1] == 0;  // codes { 0, 0 } means 'send to nowhere'
  }
  return false;
}

void MyMesh::onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id, uint32_t timestamp,
                          const uint8_t *app_data, size_t app_data_len) {
  mesh::Mesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len); // chain to super impl

  // if this a zero hop advert (and not via 'Share'), add it to neighbours
  if (packet->getPathHashCount() == 0 && !isShare(packet)) {
    AdvertDataParser parser(app_data, app_data_len);
    if (parser.isValid() && parser.getType() == ADV_TYPE_REPEATER) { // just keep neigbouring Repeaters
      putNeighbour(id, timestamp, packet->getSNR());
    }
  }
}

void MyMesh::onPeerDataRecv(mesh::Packet *packet, uint8_t type, int sender_idx, const uint8_t *secret,
                            uint8_t *data, size_t len) {
  int i = matching_peer_indexes[sender_idx];
#if defined(WITH_MQTT_NEIGHBORS)
  // Overlay response: a heard neighbour (not an ACL client) answering our
  // anon-regions scope query. Consume it and stop — it is not a client packet.
  if (neighbor_discover_active && i >= NEIGHBOR_DISCOVER_PEER_BASE) {
    int oi = i - NEIGHBOR_DISCOVER_PEER_BASE;
    if (type == PAYLOAD_TYPE_RESPONSE && oi >= 0 && oi < neighbor_discover_count) {
      handleNeighborDiscoverResponse(oi, data, len);
    }
    return;
  }
#endif
  if (i < 0 || i >= acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("onPeerDataRecv: invalid peer idx: %d", i);
    return;
  }
  ClientInfo* client = acl.getClientByIdx(i);
#if defined(WITH_MQTT_NEIGHBORS)
  // A neighbour that IS an ACL client resolves to a normal index above, so a
  // scope-query response from it lands here — match it against the overlay.
  if (neighbor_discover_active && type == PAYLOAD_TYPE_RESPONSE) {
    for (int oi = 0; oi < neighbor_discover_count; oi++) {
      if (client->id.matches(neighbor_discover[oi].id)
          && handleNeighborDiscoverResponse(oi, data, len)) {
        return;
      }
    }
  }
#endif

  if (type == PAYLOAD_TYPE_REQ) { // request (from a Known admin client!)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    if (timestamp > client->last_timestamp) { // prevent replay attacks
      int reply_len = handleRequest(client, timestamp, &data[4], len - 4);
      if (reply_len == 0) return; // invalid command

      client->last_timestamp = timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
        mesh::Packet *path = createPathReturn(client->id, secret, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
        if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      } else {
        mesh::Packet *reply =
            createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
        if (reply) {
          if (client->out_path_len != OUT_PATH_UNKNOWN) { // we have an out_path, so send DIRECT
            sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  } else if (type == PAYLOAD_TYPE_TXT_MSG && len > 5 && client->isAdmin()) { // a CLI command
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4); // timestamp (by sender's RTC clock - which could be wrong)
    uint8_t flags = (data[4] >> 2);        // message attempt number, and other flags

    if (!(flags == TXT_TYPE_PLAIN || flags == TXT_TYPE_CLI_DATA)) {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported text type received: flags=%02x", (uint32_t)flags);
    } else if (sender_timestamp >= client->last_timestamp) { // prevent replay attacks
      bool is_retry = (sender_timestamp == client->last_timestamp);
      client->last_timestamp = sender_timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      // len can be > original length, but 'text' will be padded with zeroes
      data[len] = 0; // need to make a C string again, with null terminator

      if (flags == TXT_TYPE_PLAIN) { // for legacy CLI, send Acks
        uint32_t ack_hash; // calc truncated hash of the message timestamp + text + sender pub_key, to prove
                           // to sender that we got it
        mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 5 + strlen((char *)&data[5]), client->id.pub_key,
                            PUB_KEY_SIZE);

        mesh::Packet *ack = createAck(ack_hash);
        if (ack) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFloodReply(ack, TXT_ACK_DELAY, packet->getPathHashSize());
          } else {
            sendDirect(ack, client->out_path, client->out_path_len, TXT_ACK_DELAY);
          }
        }
      }

      uint8_t temp[166];
      char *command = (char *)&data[5];
      char *reply = (char *)&temp[5];
      if (is_retry) {
        *reply = 0;
      } else {
        handleCommand(sender_timestamp, command, reply);
      }
      int text_len = strlen(reply);
      if (text_len > 0) {
        uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
        if (timestamp == sender_timestamp) {
          // WORKAROUND: the two timestamps need to be different, in the CLI view
          timestamp++;
        }
        memcpy(temp, &timestamp, 4);        // mostly an extra blob to help make packet_hash unique
        temp[4] = (TXT_TYPE_CLI_DATA << 2); // NOTE: legacy was: TXT_TYPE_PLAIN

        auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, secret, temp, 5 + text_len);
        if (reply) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFloodReply(reply, CLI_REPLY_DELAY_MILLIS, packet->getPathHashSize());
          } else {
            sendDirect(reply, client->out_path, client->out_path_len, CLI_REPLY_DELAY_MILLIS);
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  }
}

bool MyMesh::onPeerPathRecv(mesh::Packet *packet, int sender_idx, const uint8_t *secret, uint8_t *path,
                            uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len) {
  // TODO: prevent replay attacks
  int i = matching_peer_indexes[sender_idx];

  if (i >= 0 && i < acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("PATH to client, path_len=%d", (uint32_t)path_len);
    auto client = acl.getClientByIdx(i);

    // store a copy of path, for sendDirect()
    client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
    client->last_activity = getRTCClock()->getCurrentTime();
  } else {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: invalid peer idx: %d", i);
  }

  // NOTE: no reciprocal path send!!
  return false;
}

#define CTL_TYPE_NODE_DISCOVER_REQ   0x80
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90

void MyMesh::onControlDataRecv(mesh::Packet* packet) {
  uint8_t type = packet->payload[0] & 0xF0;    // just test upper 4 bits
  if (type == CTL_TYPE_NODE_DISCOVER_REQ && packet->payload_len >= 6 && discover_limiter.allow(rtc_clock.getCurrentTime())) {
    int i = 1;
    uint8_t  filter = packet->payload[i++];
    uint32_t tag;
    memcpy(&tag, &packet->payload[i], 4); i += 4;
    uint32_t since;
    if (packet->payload_len >= i+4) {   // optional since field
      memcpy(&since, &packet->payload[i], 4); i += 4;
    } else {
      since = 0;
    }

    if ((filter & (1 << ADV_TYPE_REPEATER)) != 0 && _prefs.discovery_mod_timestamp >= since) {
      bool prefix_only = packet->payload[0] & 1;
      uint8_t data[6 + PUB_KEY_SIZE];
      data[0] = CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER;   // low 4-bits for node type
      data[1] = packet->_snr;   // let sender know the inbound SNR ( x 4)
      memcpy(&data[2], &tag, 4);     // include tag from request, for client to match to
      memcpy(&data[6], self_id.pub_key, PUB_KEY_SIZE);
      auto resp = createControlData(data, prefix_only ? 6 + 8 : 6 + PUB_KEY_SIZE);
      if (resp) {
        sendZeroHop(resp, getRetransmitDelay(resp)*4);  // apply random delay (widened x4), as multiple nodes can respond to this
      }
    }
  } else if (type == CTL_TYPE_NODE_DISCOVER_RESP && packet->payload_len >= 6) {
    uint8_t node_type = packet->payload[0] & 0x0F;
    if (node_type != ADV_TYPE_REPEATER) {
      return;
    }
    if (packet->payload_len < 6 + PUB_KEY_SIZE) {
      MESH_DEBUG_PRINTLN("onControlDataRecv: DISCOVER_RESP pubkey too short: %d", (uint32_t)packet->payload_len);
      return;
    }

    if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
      pending_discover_tag = 0;
      return;
    }
    uint32_t tag;
    memcpy(&tag, &packet->payload[2], 4);
    if (tag != pending_discover_tag) {
      return;
    }

    mesh::Identity id(&packet->payload[6]);
    if (id.matches(self_id)) {
      return;
    }
    putNeighbour(id, rtc_clock.getCurrentTime(), packet->getSNR());
  }
}

void MyMesh::sendNodeDiscoverReq() {
  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ; // prefix_only=0
  data[1] = (1 << ADV_TYPE_REPEATER);
  getRNG()->random(&data[2], 4); // tag
  memcpy(&pending_discover_tag, &data[2], 4);
  pending_discover_until = futureMillis(60000);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  auto pkt = createControlData(data, sizeof(data));
  if (pkt) {
    sendZeroHop(pkt);
  }
}

MyMesh::MyMesh(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
               mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : mesh::Mesh(radio, ms, rng, rtc, *createObserverPacketManager(32), tables),
      region_map(key_store), temp_map(key_store),
      _cli(board, rtc, sensors, region_map, acl, &_prefs, this),
      telemetry(MAX_PACKET_PAYLOAD - 4),
      discover_limiter(4, 120),  // max 4 every 2 minutes
      anon_limiter(4, 180)   // max 4 every 3 minutes
#if defined(WITH_RS232_BRIDGE)
      , bridge(&_prefs, WITH_RS232_BRIDGE, _mgr, &rtc)
#elif defined(WITH_ESPNOW_BRIDGE)
      , bridge(&_prefs, _mgr, &rtc)
#elif defined(WITH_MQTT_BRIDGE)
      , bridge(nullptr)
#endif
{
  last_millis = 0;
  uptime_millis = 0;
  next_local_advert = next_flood_advert = 0;
  dirty_contacts_expiry = 0;
  set_radio_at = revert_radio_at = 0;
  _logging = false;
  region_load_active = false;
  recv_pkt_region = NULL;

#if MAX_NEIGHBOURS
  memset(neighbours, 0, sizeof(neighbours));
#endif

  // defaults
  _prefs.airtime_factor = 1.0;   // one half
  _prefs.rx_delay_base = 0.0f;   // turn off by default, was 10.0;
  _prefs.tx_delay_factor = 0.5f; // was 0.25f
  _prefs.direct_tx_delay_factor = 0.3f; // was 0.2
  StrHelper::strncpy(_prefs.node_name, ADVERT_NAME, sizeof(_prefs.node_name));
  _prefs.node_lat = ADVERT_LAT;
  _prefs.node_lon = ADVERT_LON;
  StrHelper::strncpy(_prefs.password, ADMIN_PASSWORD, sizeof(_prefs.password));
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.advert_interval = 1;        // default to 2 minutes for NEW installs
  _prefs.flood_advert_interval = 47; // 47 hours
  _prefs.flood_max = 64;
  _prefs.flood_max_unscoped = 64;
  _prefs.flood_max_advert = 8;
  _prefs.interference_threshold = 0; // disabled
#ifdef WITH_MQTT_BRIDGE
  // TODO: Re-enable this observer default once AGC reset preserves runtime
  // radio.rxgain, or earlier if disabling it causes receiver regressions.
  // _prefs.agc_reset_interval = 7;  // 28 seconds (secs/4)
#endif
  // Observer defaults (radio_watchdog, alert.*, snmp.*) moved to applyMQTTDefaults()
  // in MQTTDefaults.h — they live in /mqtt_prefs now, not NodePrefs.

  // bridge defaults
  _prefs.bridge_enabled = 1;    // enabled
  _prefs.bridge_delay   = 500;  // milliseconds
  _prefs.bridge_pkt_src = 1;    // logRx (RX packets)
  _prefs.bridge_baud = 115200;  // baud rate
  _prefs.bridge_channel = 1;    // channel 1

  StrHelper::strncpy(_prefs.bridge_secret, "LVSITANOS", sizeof(_prefs.bridge_secret));

  // GPS defaults
  _prefs.gps_enabled = 0;
  _prefs.gps_interval = 0;
  _prefs.advert_loc_policy = ADVERT_LOC_PREFS;

  // MQTT/WiFi/timezone/radio_watchdog defaults live in /mqtt_prefs now (see applyMQTTDefaults).

  _prefs.adc_multiplier = 0.0f; // 0.0f means use default board multiplier

#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default;
#endif
#endif
  _prefs.radio_fem_rxgain = 1;      // LoRa FEM RX gain on by default (FEM boards)
  _prefs.radio_fem_txgain = 0;      // LoRa FEM TX gain off by default (FEM boards)
  _prefs.cad_enabled = 0;           // hardware CAD before TX (off by default; 'set cad on')

  pending_discover_tag = 0;
  pending_discover_until = 0;

#if defined(WITH_MQTT_NEIGHBORS)
  neighbor_discover_count = 0;
  neighbor_discover_next = 0;
  neighbor_discover_publish_count = 0;
  neighbor_discover_queried_count = 0;
  neighbor_discover_json_size = 0;
  neighbor_discover_truncated = false;
  neighbor_discover_active = false;
  neighbor_table_refresh_active = false;
  neighbor_table_refresh_periodic = false;
  neighbor_discover_until = 0;
  neighbor_discover_request = NULL;
  next_neighbors_publish = 0;
  self_scopes_buf[0] = 0;
  self_default_scope_buf[0] = 0;
  neighbor_discover_origin[0] = 0;
#endif

  memset(default_scope.key, 0, sizeof(default_scope.key));
}

void MyMesh::begin(FILESYSTEM *fs) {
  mesh::Mesh::begin();
  _fs = fs;
  // load persisted prefs
  _cli.loadPrefs(_fs);

#ifdef SIM_WIFI_SSID
  // Emulator builds (Wokwi) boot with fresh NVS every run. Seed WiFi so the
  // observer auto-joins the simulator's network and brings the MQTT bridge up
  // (WiFi is driven by the bridge task), instead of raising the setup AP that
  // the emulator can't model. No-op for real firmware (flag never defined).
  {
    MQTTPrefs* obs = _cli.getObserverPrefs();
    if (obs->wifi_ssid[0] == 0) {
      strncpy(obs->wifi_ssid, SIM_WIFI_SSID, sizeof(obs->wifi_ssid) - 1);
      obs->wifi_ssid[sizeof(obs->wifi_ssid) - 1] = 0;
  #ifdef SIM_WIFI_PWD
      strncpy(obs->wifi_password, SIM_WIFI_PWD, sizeof(obs->wifi_password) - 1);
      obs->wifi_password[sizeof(obs->wifi_password) - 1] = 0;
  #endif
      _prefs.bridge_enabled = 1;   // WiFi comes up via the MQTT bridge task
    }
  }
#endif

  acl.load(_fs, self_id);
  // TODO: key_store.begin();
  region_map.load(_fs);

  // establish default-scope
  {
    RegionEntry* r = region_map.getDefaultRegion();
    if (r) {
      region_map.getTransportKeysFor(*r, &default_scope, 1);
    } else {
#ifdef DEFAULT_FLOOD_SCOPE_NAME
      r = region_map.findByName(DEFAULT_FLOOD_SCOPE_NAME);
      if (r == NULL) {
        r = region_map.putRegion(DEFAULT_FLOOD_SCOPE_NAME, 0);  // auto-create the default scope region
        if (r) { r->flags = 0; }   // Allow-flood
      }
      if (r) {
        region_map.setDefaultRegion(r);
        region_map.getTransportKeysFor(*r, &default_scope, 1);
      }
#endif
    }
  }

#if defined(WITH_BRIDGE)
  if (_prefs.bridge_enabled) {
#ifdef WITH_MQTT_BRIDGE
    // Defer construction to avoid static init crashes on ESP32 classic
    bridge = new MQTTBridge(&_prefs, _cli.getObserverPrefs(), _mgr, getRTCClock(), &self_id);
#endif
    if (bridge) {
      // Set device public key for MQTT topics
      char device_id[65];
      mesh::LocalIdentity self_id = getSelfId();
      mesh::Utils::toHex(device_id, self_id.pub_key, PUB_KEY_SIZE);
      MESH_DEBUG_PRINTLN("Setting device ID: %s", device_id);
      bridge->setDeviceID(device_id);

      // Set firmware version
      bridge->setFirmwareVersion(getFirmwareVer());

      // Set board model
      bridge->setBoardModel(_cli.getBoard()->getManufacturerName());

      // Set build date
      bridge->setBuildDate(getBuildDate());

#ifdef WITH_MQTT_BRIDGE
      // Set stats sources for automatic stats collection
      bridge->setStatsSources(this, _radio, _cli.getBoard(), _ms);
#ifdef WITH_SNMP
      if (_cli.getObserverPrefs()->snmp_enabled) {
        _snmp_agent.setNodeName(_prefs.node_name);
        _snmp_agent.setFirmwareVersion(getFirmwareVer());
        bridge->setSNMPAgent(&_snmp_agent);
      }
#endif
#endif

      bridge->begin();
    }
  }
#endif

  // Wire fault-alert reporter. begin() is safe regardless of bridge state.
  // Passing `this` as the callbacks lets the reporter resolve a TransportKey
  // scope (alert.region override, falling back to default_scope) so alert
  // floods ride the same scope as adverts/channel messages.
#ifdef WITH_MQTT_BRIDGE
  _alerter.begin(&_prefs, _cli.getObserverPrefs(), this, this);
  _alerter.setBridge(bridge);
#endif

#if defined(WITH_WEBCONFIG) && !defined(WEBCONFIG_NO_AUTO_AP)
  // First-boot setup portal: raised only when no WiFi has ever been configured,
  // so an OTA onto a deployed (configured) node can never open an AP.
  if (_cli.getObserverPrefs()->wifi_ssid[0] == 0) {
    char wc_reply[160];
    startWebConfig(false, wc_reply);
    Serial.println(wc_reply);
  }
#endif

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);

  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");
  board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain);   // LoRa FEM LNA (FEM boards only)
  board.setLoRaFemPaGainEnabled(_prefs.radio_fem_txgain);

  updateAdvertTimer();
  updateFloodAdvertTimer();

#if ENV_INCLUDE_GPS == 1
  applyGpsPrefs();
#endif
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, path_hash_size);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, path_hash_size);
  }
}

bool MyMesh::resolveAlertScope(TransportKey& dest) {
  // Prefer an explicit alert.region override; look it up lazily via
  // RegionMap so the operator can name a region that doesn't exist yet
  // without polluting region_map state — we just silently fall through
  // to default_scope on miss.
#ifdef WITH_MQTT_BRIDGE
  const char* alert_region = _cli.getObserverPrefs()->alert_region;
  if (alert_region[0]) {
    auto r = region_map.findByNamePrefix(alert_region);
    if (r && region_map.getTransportKeysFor(*r, &dest, 1) > 0 && !dest.isNull()) {
      return true;
    }
  }
#endif
  if (!default_scope.isNull()) {
    dest = default_scope;
    return true;
  }
  return false;
}

void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
  set_radio_at = futureMillis(2000); // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;

  revert_radio_at = futureMillis(2000 + timeout_mins * 60 * 1000); // schedule when to revert radio params
}

bool MyMesh::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
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

void MyMesh::sendSelfAdvertisement(int delay_millis, bool flood) {
  mesh::Packet *pkt = createSelfAdvert();
  if (pkt) {
    if (flood) {
      sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);
    } else {
      sendZeroHop(pkt, delay_millis);
    }
  } else {
    MESH_DEBUG_PRINTLN("ERROR: unable to create advertisement packet!");
  }
}

void MyMesh::updateAdvertTimer() {
  if (_prefs.advert_interval > 0) { // schedule local advert timer
    next_local_advert = futureMillis((int)((uint32_t)_prefs.advert_interval * 2 * 60 * 1000));
  } else {
    next_local_advert = 0; // stop the timer
  }
}

void MyMesh::updateFloodAdvertTimer() {
  if (_prefs.flood_advert_interval > 0) { // schedule flood advert timer
    next_flood_advert = futureMillis(((uint32_t)_prefs.flood_advert_interval) * 60 * 60 * 1000);
  } else {
    next_flood_advert = 0; // stop the timer
  }
}

void MyMesh::dumpLogFile() {
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

void MyMesh::setTxPower(int8_t power_dbm) {
  radio_driver.setTxPower(power_dbm);
}

bool MyMesh::setRxBoostedGain(bool enable) {
  return radio_driver.setRxBoostedGainMode(enable);
}

#if defined(USE_LR2021)
bool MyMesh::configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) {
  return radio_driver.configSideDetectors(sideDetSFs, num, bw);
}
#endif

void MyMesh::formatNeighborsReply(char *reply) {
  char *dp = reply;

#if MAX_NEIGHBOURS
  // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
  int16_t neighbours_count = 0;
  NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    auto neighbour = &neighbours[i];
    if (neighbour->heard_timestamp > 0) {
      sorted_neighbours[neighbours_count] = neighbour;
      neighbours_count++;
    }
  }

  // sort neighbours newest to oldest
  std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
    return a->heard_timestamp > b->heard_timestamp; // desc
  });

  for (int i = 0; i < neighbours_count && dp - reply < 134; i++) {
    NeighbourInfo *neighbour = sorted_neighbours[i];

    // add new line if not first item
    if (i > 0) *dp++ = '\n';

    char hex[10];
    // get 4 bytes of neighbour id as hex
    mesh::Utils::toHex(hex, neighbour->id.pub_key, 4);

    // add next neighbour
    uint32_t secs_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
    sprintf(dp, "%s:%d:%d", hex, secs_ago, neighbour->snr);
    while (*dp)
      dp++; // find end of string
  }
#endif
  if (dp == reply) { // no neighbours, need empty response
    strcpy(dp, "-none-");
    dp += 6;
  }
  *dp = 0; // null terminator
}

void MyMesh::removeNeighbor(const uint8_t *pubkey, int key_len) {
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    NeighbourInfo *neighbour = &neighbours[i];
    if (memcmp(neighbour->id.pub_key, pubkey, key_len) == 0) {
      neighbours[i] = NeighbourInfo(); // clear neighbour entry
    }
  }
#endif
}

void MyMesh::startRegionsLoad() {
  temp_map.resetFrom(region_map);   // rebuild regions in a temp instance
  memset(load_stack, 0, sizeof(load_stack));
  load_stack[0] = &temp_map.getWildcard();
  region_load_active = true;
}

bool MyMesh::saveRegions() {
  return region_map.save(_fs);
}

void MyMesh::onDefaultRegionChanged(const RegionEntry* r) {
  if (r) {
    region_map.getTransportKeysFor(*r, &default_scope, 1);
  } else {
    memset(default_scope.key, 0, sizeof(default_scope.key));
  }
}

void MyMesh::formatStatsReply(char *reply) {
  StatsFormatHelper::formatCoreStats(reply, board, *_ms, _err_flags, _mgr);
}

void MyMesh::formatRadioStatsReply(char *reply) {
  StatsFormatHelper::formatRadioStats(reply, _radio, radio_driver, getTotalAirTime(), getReceiveAirTime());
}

void MyMesh::formatRadioDiagReply(char *reply) {
  StatsFormatHelper::formatRadioDiag(reply, _radio, radio_driver, *_ms, _err_flags, hasOutbound());
}

void MyMesh::formatPacketStatsReply(char *reply) {
  StatsFormatHelper::formatPacketStats(reply, radio_driver, getNumSentFlood(), getNumSentDirect(), 
                                       getNumRecvFlood(), getNumRecvDirect());
}

void MyMesh::saveIdentity(const mesh::LocalIdentity &new_id) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  IdentityStore store(*_fs, "");
#elif defined(ESP32)
  IdentityStore store(*_fs, "/identity");
#elif defined(RP2040_PLATFORM)
  IdentityStore store(*_fs, "/identity");
#else
#error "need to define saveIdentity()"
#endif
  store.save("_main", new_id);
}

void MyMesh::clearStats() {
  radio_driver.resetStats();
  resetStats();
  ((SimpleMeshTables *)getTables())->resetStats();
}

#ifdef WITH_WEBCONFIG
bool MyMesh::startWebConfig(bool force_ap, char* reply) {
  if (_webconfig && (_webconfig->isRunning() || _webconfig->isStopping())) {
    strcpy(reply, _webconfig->isStopping() ? "Err: webconfig still stopping, retry shortly"
                                           : "Err: webconfig already running");
    return true;
  }
  if (!_webconfig) {
    _webconfig = new WebConfigServer(&_prefs, _cli.getObserverPrefs(), this,
                                     self_id.pub_key, getFirmwareVer(), getBuildDate(), getRole(),
                                     _cli.getBoard()->getManufacturerName());
  }
  if (force_ap) {
    // The setup AP owns WiFi outright; refuse while the bridge holds the STA.
    if (bridge && bridge->isRunning()) {
      strcpy(reply, "Err: MQTT bridge is running - 'set bridge off' first");
      return true;
    }
    _webconfig->startSetupMode(reply);
  } else if (_cli.getObserverPrefs()->wifi_ssid[0] == 0) {
    _webconfig->startSetupMode(reply);   // unconfigured: same portal as first boot
  } else {
    _webconfig->startLanMode(reply);     // reports "WiFi not connected" if down
  }
  return true;
}

bool MyMesh::stopWebConfig(char* reply) {
  if (!_webconfig || !_webconfig->isRunning()) {
    strcpy(reply, "Err: webconfig not running");
    return true;
  }
  _webconfig->requestStop();
  strcpy(reply, "OK - webconfig stopping");
  return true;
}

void MyMesh::onConfigBatchEnd() {
  _wc_batch_active = false;
  if (_wc_restart_pending) {
    // A full restart re-applies every slot config; drop the per-slot requests.
    _wc_restart_pending = false;
    _wc_slot_restart_mask = 0;
    restartBridge();
  } else if (_wc_slot_restart_mask) {
    uint8_t mask = _wc_slot_restart_mask;
    _wc_slot_restart_mask = 0;
    for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
      if (mask & (1u << i)) restartBridgeSlot(i);
    }
  }
}

// Stats snapshot for GET /api/stats. Runs on the loop task (from tick());
// same sources as the REQ_TYPE_GET_STATUS reply and `get mqtt.stats`.
void MyMesh::buildStatsJson(char* buf, size_t buf_size) {
  char ip[20] = "";
  int wifi_rssi = 0;
  if (WiFi.status() == WL_CONNECTED) {
    strncpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip) - 1);
    wifi_rssi = WiFi.RSSI();
  } else if (_webconfig && _webconfig->mode() == WebConfigServer::MODE_SETUP) {
    strncpy(ip, WiFi.softAPIP().toString().c_str(), sizeof(ip) - 1);
  }
  int pos = snprintf(buf, buf_size,
      "{\"uptime_s\":%lu,\"batt_mv\":%u,"
      "\"heap_free\":%lu,\"heap_min\":%lu,\"heap_max_alloc\":%lu,"
      "\"noise\":%d,\"rssi\":%d,\"snr\":%.1f,"
      "\"airtime_s\":%lu,\"rx_airtime_s\":%lu,"
      "\"recv\":%lu,\"sent\":%lu,\"rx_err\":%lu,"
      "\"sent_flood\":%lu,\"sent_direct\":%lu,\"recv_flood\":%lu,\"recv_direct\":%lu,"
      "\"tx_queue\":%d,\"wifi_rssi\":%d,\"ip\":\"%s\",\"mqtt_queue\":%d,\"slots\":[",
      (unsigned long)(uptime_millis / 1000), (unsigned)board.getBattMilliVolts(),
      (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
      (unsigned long)ESP.getMaxAllocHeap(),
      (int)_radio->getNoiseFloor(), (int)radio_driver.getLastRSSI(),
      radio_driver.getLastSNR(),
      (unsigned long)(getTotalAirTime() / 1000), (unsigned long)(getReceiveAirTime() / 1000),
      (unsigned long)radio_driver.getPacketsRecv(), (unsigned long)radio_driver.getPacketsSent(),
      (unsigned long)radio_driver.getPacketsRecvErrors(),
      (unsigned long)getNumSentFlood(), (unsigned long)getNumSentDirect(),
      (unsigned long)getNumRecvFlood(), (unsigned long)getNumRecvDirect(),
      (int)_mgr->getOutboundCount(0xFFFFFFFF), wifi_rssi, ip,
      bridge ? bridge->getQueueSize() : 0);
  if (pos < 0 || pos >= (int)buf_size - 3) return;  // truncated; snprintf terminated it
  bool first = true;
  for (int i = 0; i < RUNTIME_MQTT_SLOTS; i++) {
    MQTTBridge::SlotStatusSnapshot s;
    if (!MQTTBridge::getSlotStatusSnapshot(i, &s)) continue;
    // "filt" is omitted for the all-types default, so the portal only has to
    // render the exception and the JSON stays inside the stats buffer.
    char filt[24];
    filt[0] = '\0';
    if (s.filter_mask != MQTTPacketFilter::kAllPacketTypes) {
      snprintf(filt, sizeof(filt), ",\"filt\":%u", (unsigned)s.filter_mask);
    }
    int n = snprintf(buf + pos, buf_size - pos,
                     "%s{\"n\":%d,\"name\":\"%s\",\"state\":\"%s\",\"ok\":%lu,\"err\":%lu%s}",
                     first ? "" : ",", i + 1, s.name, s.state, s.publish_ok, s.publish_err,
                     filt);
    if (n < 0 || n >= (int)(buf_size - pos)) break;
    pos += n;
    first = false;
  }
  snprintf(buf + pos, buf_size - pos, "]}");
}
#endif

void MyMesh::handleCommand(uint32_t sender_timestamp, char *command, char *reply) {
  if (region_load_active) {
    if (StrHelper::isBlank(command)) {  // empty/blank line, signal to terminate 'load' operation
      region_map = temp_map;  // copy over the temp instance as new current map
      region_load_active = false;

      sprintf(reply, "OK - loaded %d regions", region_map.getCount());
    } else {
      char *np = command;
      while (*np == ' ') np++;   // skip indent
      int indent = np - command;

      char *ep = np;
      while (RegionMap::is_name_char(*ep)) ep++;
      if (*ep) { *ep++ = 0; }  // set null terminator for end of name

      while (*ep && *ep != 'F') ep++;  // look for (optional) flags

      if (indent > 0 && indent < 8 && strlen(np) > 0) {
        auto parent = load_stack[indent - 1];
        if (parent) {
          auto old = region_map.findByName(np);
          auto nw = temp_map.putRegion(np, parent->id, old ? old->id : 0);  // carry-over the current ID (if name already exists)
          if (nw) {
            nw->flags = old ? old->flags : (*ep == 'F' ? 0 : REGION_DENY_FLOOD);   // carry-over flags from curr

            load_stack[indent] = nw;  // keep pointers to parent regions, to resolve parent_id's
          }
        }
      }
      reply[0] = 0;
    }
    return;
  }

  while (*command == ' ') command++; // skip leading spaces

  if (strlen(command) > 4 && command[2] == '|') { // optional prefix (for companion radio CLI)
    memcpy(reply, command, 3);                    // reflect the prefix back
    reply += 3;
    command += 3;
  }

  // handle ACL related commands
  if (memcmp(command, "setperm ", 8) == 0) {   // format:  setperm {pubkey-hex} {permissions-int8}
    char* hex = &command[8];
    char* sp = strchr(hex, ' ');   // look for separator char
    if (sp == NULL) {
      strcpy(reply, "Err - bad params");
    } else {
      *sp++ = 0;   // replace space with null terminator

      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min(sp - hex, PUB_KEY_SIZE*2);
      if (mesh::Utils::fromHex(pubkey, hex_len / 2, hex)) {
        uint8_t perms = atoi(sp);
        if (acl.applyPermissions(self_id, pubkey, hex_len / 2, perms)) {
          dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);   // trigger acl.save()
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Err - invalid params");
        }
      } else {
        strcpy(reply, "Err - bad pubkey");
      }
    }
  } else if (sender_timestamp == 0 && strcmp(command, "get acl") == 0) {
    Serial.println("ACL:");
    for (int i = 0; i < acl.getNumClients(); i++) {
      auto c = acl.getClientByIdx(i);
      if (c->permissions == 0) continue;  // skip deleted (or guest) entries

      Serial.printf("%02X ", c->permissions);
      mesh::Utils::printHex(Serial, c->id.pub_key, PUB_KEY_SIZE);
      Serial.printf("\n");
    }
    reply[0] = 0;
  } else if (memcmp(command, "discover.neighbors", 18) == 0) {
    const char* sub = command + 18;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.neighbors has no options");
    } else {
      sendNodeDiscoverReq();
      strcpy(reply, "OK - Discover sent");
    }
#if defined(WITH_MQTT_NEIGHBORS)
  } else if (memcmp(command, "discover.scopes", 15) == 0) {
    const char* sub = command + 15;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.scopes has no options");
    } else if (pending_discover_tag != 0 &&
               !millisHasNowPassed(pending_discover_until) &&
               !neighbor_discover_active) {
      // A zero-hop table refresh is already collecting; queue the scope pass
      // behind it (as a manual, non-periodic request) rather than starting a
      // second refresh.
      if (!neighborDiscoverReady(reply)) {
        // reply already set by neighborDiscoverReady
      } else {
        neighbor_table_refresh_active = true;
        neighbor_table_refresh_periodic = false;
        long remaining_ms = (long)(pending_discover_until - futureMillis(0));
        unsigned remaining_secs = remaining_ms > 0
          ? (unsigned)(((unsigned long)remaining_ms + 999UL) / 1000UL) : 0;
        sprintf(reply, "OK - scopes queued (%us discovery remaining)", remaining_secs);
        MESH_DEBUG_PRINTLN("Neighbor scopes queued behind active discovery (%us remaining)", remaining_secs);
      }
    } else if (!startNeighborDiscover(reply)) {
      // reply already set by startNeighborDiscover
    }
#elif defined(WITH_MQTT_BRIDGE)
  } else if (memcmp(command, "discover.scopes", 15) == 0) {
    strcpy(reply, "Err - neighbors not enabled in this build");
#endif
  } else{
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
}

void MyMesh::loop() {
  // Check radio FIRST to ensure we don't miss incoming packets
  // MQTT processing runs in a separate FreeRTOS task on Core 0, so we don't call bridge.loop() here
  mesh::Mesh::loop();

#ifdef WITH_BRIDGE
  // bridge.loop() is now handled by FreeRTOS task on Core 0 - no need to call it here
#endif

  if (next_flood_advert && millisHasNowPassed(next_flood_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    uint32_t delay_millis = 0;
    if (pkt) sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);

    updateFloodAdvertTimer(); // schedule next flood advert
    updateAdvertTimer();      // also schedule local advert (so they don't overlap)
  } else if (next_local_advert && millisHasNowPassed(next_local_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    if (pkt) sendZeroHop(pkt);

    updateAdvertTimer(); // schedule next local advert
  }

  if (set_radio_at && millisHasNowPassed(set_radio_at)) { // apply pending (temporary) radio params
    set_radio_at = 0;                                     // clear timer
    radio_driver.setParams(pending_freq, pending_bw, pending_sf, pending_cr);
    MESH_DEBUG_PRINTLN("Temp radio params");
  }

  if (revert_radio_at && millisHasNowPassed(revert_radio_at)) { // revert radio params to orig
    revert_radio_at = 0;                                        // clear timer
    radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    MESH_DEBUG_PRINTLN("Radio params restored");
  }

#if defined(WITH_MQTT_BRIDGE) && defined(OTA_MANIFEST_BASE)
  if (_ota_update_at && millisHasNowPassed(_ota_update_at)) { // deferred `ota update`
    _ota_update_at = 0;                                       // clear timer
    // The "Beginning update..." reply has now gone out. Free the bridge for heap
    // headroom, then flash: otaFromManifest reboots into the new image on success
    // (so this never returns); on any abort (already up to date, partition change,
    // download error) it returns and we resume the bridge.
    Serial.println("OTA: starting update");
    // Flush the START alert (and CLI reply) out the radio BEFORE teardown blocks
    // the loop until reboot — otherwise a packet still queued here (busy /
    // duty-limited channel) is lost when the flash spins the loop and reboots.
    drainOutbound(OTA_TX_DRAIN_TIMEOUT_MS);
    setBridgeState(false);
    char ota_reply[160];
    // OTA teardown barrier (Phase 5): only flash after a CLEAN MQTT shutdown.
    // A timed-out/forced stop leaves mbedTLS/heap ownership uncertain — writing
    // firmware then is the observed teardown heap-panic path — so abort and
    // resume the bridge instead of flashing under uncertain ownership.
    if (bridge && !bridge->canFlashAfterStop()) {
      Serial.println("OTA: aborted, MQTT stop did not complete cleanly - resuming bridge");
      otaAlert("OTA aborted: MQTT stop unclean, bridge resumed");
      setBridgeState(true);
    } else if (!_cli.getBoard()->otaFromManifest(getFirmwareVer(), false, ota_reply)) {
      Serial.print("OTA: aborted, resuming bridge - "); Serial.println(ota_reply);
      char ota_alert_msg[160];
      snprintf(ota_alert_msg, sizeof(ota_alert_msg), "OTA aborted: %s", ota_reply);
      otaAlert(ota_alert_msg);
      setBridgeState(true);
    }
    // Success path: otaFromManifest() flashes and reboots into the new image
    // (never returns), so there is no in-boot "success" alert — the START alert
    // plus the node returning on the new version is the success signal.
  }
#endif

#ifdef WITH_WEBCONFIG
  if (_webconfig) {
    _webconfig->tick(millis());
    if (!_webconfig->isRunning() && !_webconfig->isStopping()) {
      delete _webconfig;   // teardown finished (or start failed): reclaim the heap
      _webconfig = NULL;
    }
  }
#endif

  // is pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    acl.save(_fs);
    dirty_contacts_expiry = 0;
  }

  // update uptime
  uint32_t now = millis();
  uptime_millis += now - last_millis;
  last_millis = now;

#ifdef WITH_MQTT_BRIDGE
  _alerter.onLoop(now);
#endif

#if defined(WITH_MQTT_NEIGHBORS)
  // Two-stage periodic neighbors publication:
  //   stage 1 - zero-hop node-discover refreshes the neighbour table (60s window)
  //   stage 2 - anon-regions scope query per neighbour (startNeighborDiscover)
  // then the table JSON is published and the next cycle is rescheduled.
  bool periodic_neighbors_enabled = _cli.getObserverPrefs()->mqtt_neighbors_enabled;
  if (neighbor_discover_active) {
    loopNeighborDiscover();
  } else if (neighbor_table_refresh_active) {
    if (neighbor_table_refresh_periodic && !periodic_neighbors_enabled) {
      // periodic switched off mid-refresh -> cancel (leave pending_discover_tag alone)
      neighbor_table_refresh_active = false;
      neighbor_table_refresh_periodic = false;
      next_neighbors_publish = 0;
    } else if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
      // 60s zero-hop window done -> begin the per-neighbour scope queries
      bool was_periodic = neighbor_table_refresh_periodic;
      pending_discover_tag = 0;
      neighbor_table_refresh_active = false;
      neighbor_table_refresh_periodic = false;
      char tmp_reply[80];
      const char* origin_str = was_periodic ? "periodic" : "manual";
      if (startNeighborDiscover(tmp_reply)) {
        MESH_DEBUG_PRINTLN("MQTT %s %s", origin_str, tmp_reply);
      } else {
        if (periodic_neighbors_enabled) {
          next_neighbors_publish = futureMillis(_cli.getObserverPrefs()->mqtt_neighbors_interval);
        }
        MESH_DEBUG_PRINTLN("MQTT %s neighbor scope discovery failed: %s", origin_str, tmp_reply);
      }
    }
  } else if (periodic_neighbors_enabled && bridge && bridge->isRunning()) {
    if (next_neighbors_publish == 0 ||
        (next_neighbors_publish != 0 && millisHasNowPassed(next_neighbors_publish))) {
      if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
        pending_discover_tag = 0;
        sendNodeDiscoverReq();
        MESH_DEBUG_PRINTLN("MQTT periodic neighbor table refresh started");
      } else {
        MESH_DEBUG_PRINTLN("MQTT periodic refresh joined active neighbor discovery");
      }
      neighbor_table_refresh_active = true;
      neighbor_table_refresh_periodic = true;
    }
  }

  // Report the schedule state back to the bridge for `get mqtt.status`.
  if (bridge) {
    if (neighbor_discover_active || neighbor_table_refresh_active) {
      bridge->setNeighborsSchedule(MQTTBridge::NBR_ACTIVE, 0);
    } else if (next_neighbors_publish == 0 || millisHasNowPassed(next_neighbors_publish)) {
      bridge->setNeighborsSchedule(MQTTBridge::NBR_DUE, 0);
    } else {
      long remaining_ms = (long)(next_neighbors_publish - futureMillis(0));
      uint32_t remaining_secs = remaining_ms > 0 ? (uint32_t)(remaining_ms / 1000) : 0;
      bridge->setNeighborsSchedule(MQTTBridge::NBR_SCHEDULED, remaining_secs);
    }
  }
#endif

#ifdef WITH_SNMP
  // Push radio stats to SNMP agent every 2 seconds
  if (_snmp_agent.isRunning()) {
    static unsigned long last_snmp_stats = 0;
    if (now - last_snmp_stats >= 2000) {
      last_snmp_stats = now;
      _snmp_agent.updateRadioStats(
        radio_driver.getPacketsRecv(), radio_driver.getPacketsSent(),
        radio_driver.getPacketsRecvErrors(),
        (int16_t)_radio->getNoiseFloor(),
        (int16_t)radio_driver.getLastRSSI(),
        (int16_t)(radio_driver.getLastSNR() * 4),
        getNumSentFlood(), getNumSentDirect(),
        getNumRecvFlood(), getNumRecvDirect(),
        getTotalAirTime() / 1000, uptime_millis / 1000);
    }
  }
#endif
}

#if defined(WITH_MQTT_NEIGHBORS)
#include "helpers/MQTTMessageBuilder.h"
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

// This node's own non-flood scope names, same source the anon-regions server
// reply uses. Empty string when the node has no scoped regions.
void MyMesh::getLocalScopes(char* buf, size_t len) {
  if (!buf || len == 0) return;
  buf[0] = 0;
  region_map.exportNamesTo(buf, (int)len, REGION_DENY_FLOOD);
}

// Client side of the anon-regions request (the server side is handleAnonRegionsReq).
// Inner payload: {tag(4)}{ANON_REQ_TYPE_REGIONS}{0x00 = zero-hop reply path}.
mesh::Packet* MyMesh::sendAnonRegionsReq(const mesh::Identity& target, uint32_t& tag) {
  // RxReservePacketManager keeps a four-packet emergency floor. Preflight one
  // extra free packet so its void queue API cannot silently shed this request.
  if (_mgr->getFreeCount() < NEIGHBOR_DISCOVER_MIN_FREE_PACKETS) return NULL;

  uint8_t secret[PUB_KEY_SIZE];
  self_id.calcSharedSecret(secret, target);

  tag = getRTCClock()->getCurrentTimeUnique();
  uint8_t inner[6];
  memcpy(inner, &tag, 4);
  inner[4] = ANON_REQ_TYPE_REGIONS;
  inner[5] = 0x00; // request a zero-hop reply path

  mesh::Packet* pkt = createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, target, secret, inner, sizeof(inner));
  if (!pkt) return NULL;
  sendDirect(pkt, NULL, 0, 0);
  return pkt;
}

bool MyMesh::cancelNeighborDiscoverRequest() {
  if (!neighbor_discover_request) return false;
  for (int i = _mgr->getOutboundTotal() - 1; i >= 0; i--) {
    if (_mgr->getOutboundByIdx(i) == neighbor_discover_request) {
      mesh::Packet* pkt = _mgr->removeOutboundByIdx(i);
      if (pkt) releasePacket(pkt);
      neighbor_discover_request = NULL;
      return true;
    }
  }
  return false;
}

// This timer starts after the request finishes transmitting. Allow the server
// delay, the responder's full CAD deferral window plus one maximum retry
// overshoot, and airtime for one priority-0 packet ahead of the response plus
// the response itself. The radio estimate scales with SF, bandwidth, coding
// rate, and preamble.
uint32_t MyMesh::neighborDiscoverQueryTimeoutMs() const {
  uint32_t response_airtime = _radio->getEstAirtimeFor(MAX_PACKET_PAYLOAD + 2);
  return SERVER_RESPONSE_DELAY + getCADFailMaxDuration() + 360UL
    + response_airtime * 2UL;
}

void MyMesh::resetNeighborDiscoverJsonBudget() {
  getLocalScopes(self_scopes_buf, sizeof(self_scopes_buf));
  {
    // No default region means this node floods unscoped, i.e. the wildcard.
    RegionEntry* def = region_map.getDefaultRegion();
    const char* def_name = (def && def->name[0]) ? def->name : "*";
    if (*def_name == '#') def_name++;  // match how self.scopes renders names
    strncpy(self_default_scope_buf, def_name, sizeof(self_default_scope_buf) - 1);
    self_default_scope_buf[sizeof(self_default_scope_buf) - 1] = 0;
  }
  MQTTBridge::getEffectiveMqttOrigin(
    &_prefs, _cli.getObserverPrefs(),
    neighbor_discover_origin, sizeof(neighbor_discover_origin));

  char self_pubkey_hex[65];
  mesh::Utils::toHex(self_pubkey_hex, self_id.pub_key, PUB_KEY_SIZE);
  char timestamp[40];
  MQTTMessageBuilder::formatIsoTimestampForMqtt(
    getRTCClock()->getCurrentTime(), 0, nullptr, timestamp, sizeof(timestamp));

  neighbor_discover_publish_count = 0;
  neighbor_discover_queried_count = 0;
  neighbor_discover_truncated = false;
  neighbor_discover_json_size = MQTTMessageBuilder::measureNeighborsMessageBase(
    neighbor_discover_origin, self_pubkey_hex, timestamp, self_scopes_buf,
    self_default_scope_buf, neighbor_discover_count);
}

// Account for one terminal result. The base measurement reserves maximum-width
// progress metadata; UINT32_MAX likewise reserves the widest heard-age value.
// If this result cannot fit, stop before transmitting another scope request.
bool MyMesh::completeNeighborDiscoverEntry() {
  NeighborDiscoverEntry& entry = neighbor_discover[neighbor_discover_next];
  char pubkey_hex[65];
  mesh::Utils::toHex(pubkey_hex, entry.id.pub_key, PUB_KEY_SIZE);

  MQTTMessageBuilder::NeighborsMessageEntry measured = {
    pubkey_hex,
    entry.snr / 4.0f,
    UINT32_MAX,
    entry.scopes,
    entry.status == ND_RESPONDED ? "responded"
      : (entry.status == ND_SEND_FAILED ? "send_failed" : "timeout")
  };
  size_t added = MQTTMessageBuilder::measureNeighborsMessageEntry(measured);
  if (neighbor_discover_publish_count > 0) added++;  // array comma

  if (neighbor_discover_json_size + added >= MQTTBridge::NEIGHBORS_JSON_BUFFER_SIZE ||
      neighbor_discover_publish_count >= MQTTBridge::NEIGHBORS_MAX_PUBLISH_ENTRIES) {
    neighbor_discover_truncated = true;
    finishNeighborDiscover();
    return false;
  }

  neighbor_discover_json_size += added;
  neighbor_discover_publish_count++;
  neighbor_discover_next++;
  return true;
}

// Match a RESPONSE against the pending overlay entry by tag; copy its scope
// string (payload after the 8-byte {tag}{clock} header) into the entry.
bool MyMesh::handleNeighborDiscoverResponse(int overlay_idx, const uint8_t* data, size_t len) {
  if (overlay_idx < 0 || overlay_idx >= neighbor_discover_count) return false;
  NeighborDiscoverEntry& entry = neighbor_discover[overlay_idx];
  if (entry.status != ND_PENDING || len < 8) return false;

  uint32_t tag;
  memcpy(&tag, data, 4);
  if (tag != entry.tag) return false;

  size_t scope_len = len - 8;
  if (scope_len >= sizeof(entry.scopes)) {
    scope_len = sizeof(entry.scopes) - 1;
  }
  memcpy(entry.scopes, &data[8], scope_len);
  entry.scopes[scope_len] = 0;
  entry.status = ND_RESPONDED;
  // A zero-hop reply is proof we heard this neighbour now, so re-stamp both the
  // snapshot and the live table; a stamp taken before time sync heals here.
  entry.heard_timestamp = getRTCClock()->getCurrentTime();
  touchNeighbourHeard(entry.id, entry.heard_timestamp);
  return true;
}

// Refresh a live neighbour's heard time only: a scope reply carries no advert
// timestamp or SNR to update.
void MyMesh::touchNeighbourHeard(const mesh::Identity& id, uint32_t heard_timestamp) {
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (id.matches(neighbours[i].id)) {
      neighbours[i].heard_timestamp = heard_timestamp;
      return;
    }
  }
#endif
}

// A heard age is a wall-clock delta, so it only means something when both stamps
// share a clock epoch. An entry heard before the clock was set holds the unset
// default, which a synced clock turns into a ~2-year age; report those as
// unknown instead. See UPSTREAM_BUGS.md for the monotonic fix.
static bool neighborHeardAgeUsable(uint32_t heard_timestamp, uint32_t now_secs) {
  if (heard_timestamp == 0 || now_secs < heard_timestamp) return false;
  // Never synced: the stamp shares this clock's boot epoch, so the delta holds.
  if (now_secs < MQTTConnectionPolicy::kSyncedClockEpoch) return true;
  return heard_timestamp >= MQTTConnectionPolicy::kSyncedClockEpoch;
}

// Publish-ordering: usable ages first, then most recently heard, then stronger
// SNR, then pubkey. The JSON builder drops the tail if the buffer fills, so the
// head must be the most useful entries.
static bool neighborPublishEntryComesBefore(
    const MQTTMessageBuilder::NeighborsMessageEntry& lhs,
    const MQTTMessageBuilder::NeighborsMessageEntry& rhs) {
  if (lhs.heard_unknown != rhs.heard_unknown) {
    return !lhs.heard_unknown;
  }
  if (lhs.heard_secs_ago != rhs.heard_secs_ago) {
    return lhs.heard_secs_ago < rhs.heard_secs_ago;  // newer first
  }
  if (lhs.snr != rhs.snr) {
    return lhs.snr > rhs.snr;  // stronger first when equally recent
  }
  return strcmp(lhs.pubkey_hex, rhs.pubkey_hex) < 0;
}

#if defined(ESP_PLATFORM)
// Neighbors allocations prefer PSRAM where it exists and otherwise come from
// internal DRAM, so MQTT_NEIGHBORS_WITHOUT_PSRAM boards can build the table too.
#if defined(BOARD_HAS_PSRAM)
static const uint32_t kNeighborsAllocCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
static const uint32_t kNeighborsAllocCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#endif

static void* neighborsAlloc(size_t size) {
  if (size == 0) return nullptr;
  void* p = heap_caps_malloc(size, kNeighborsAllocCaps);
#if defined(BOARD_HAS_PSRAM)
  if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
  return p;
}

static void neighborsFree(void* ptr) {
  if (ptr) heap_caps_free(ptr);
}

// ArduinoJson v7 JsonDocument has no real capacity cap (DynamicJsonDocument(N)
// is a no-op shim), so soft-cap peak pool growth to NEIGHBORS_DOC_POOL_BUDGET.
// used only rises on allocate — conservative for this single-shot doc (overflow
// path removes+breaks, so no further growth after free).
struct NeighborsDocAllocator : ArduinoJson::Allocator {
  size_t used = 0;
  static const size_t kBudget = MQTTBridge::NEIGHBORS_DOC_POOL_BUDGET;

  void* allocate(size_t size) override {
    if (used >= kBudget || size > kBudget - used) return nullptr;
    void* p = neighborsAlloc(size);
    if (p) used += size;
    return p;
  }

  void deallocate(void* ptr) override {
    neighborsFree(ptr);
  }

  void* reallocate(void* ptr, size_t new_size) override {
    size_t old_size = ptr ? heap_caps_get_allocated_size(ptr) : 0;
    size_t next_used = (used >= old_size) ? (used - old_size) : 0;
    if (next_used >= kBudget || new_size > kBudget - next_used) return nullptr;
    void* p = heap_caps_realloc(ptr, new_size, kNeighborsAllocCaps);
#if defined(BOARD_HAS_PSRAM)
    if (!p) p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    if (p) used = next_used + new_size;
    return p;
  }
};
#else
static void* neighborsAlloc(size_t size) { return size ? malloc(size) : nullptr; }
static void neighborsFree(void* ptr) { free(ptr); }
#endif

// Build the neighbors-table JSON and hand it to the bridge, then reschedule.
void MyMesh::finishNeighborDiscover() {
  char self_pubkey_hex[65];
  mesh::Utils::toHex(self_pubkey_hex, self_id.pub_key, PUB_KEY_SIZE);

  char timestamp[40];
  MQTTMessageBuilder::formatIsoTimestampForMqtt(getRTCClock()->getCurrentTime(), 0, nullptr, timestamp, sizeof(timestamp));

  // The entry table plus one hex string each reaches ~4.5 KB at MAX_NEIGHBOURS,
  // which does not fit the mesh loop task's 8 KB stack, so both share a single
  // heap block sized to this pass. Publishing is skipped if either alloc fails.
  const int publish_count = neighbor_discover_publish_count;
  const size_t hex_size = PUB_KEY_SIZE * 2 + 1;
  const size_t entries_bytes =
    sizeof(MQTTMessageBuilder::NeighborsMessageEntry) * publish_count;
  void* scratch = neighborsAlloc(entries_bytes + hex_size * publish_count);
  char* json_buf = (char*)neighborsAlloc(MQTTBridge::NEIGHBORS_JSON_BUFFER_SIZE);

  if (json_buf && (scratch || publish_count == 0)) {
    auto* entries = (MQTTMessageBuilder::NeighborsMessageEntry*)scratch;
    char* pubkey_hex = (char*)scratch + entries_bytes;
    uint32_t now_secs = getRTCClock()->getCurrentTime();

    for (int i = 0; i < publish_count; i++) {
      auto& entry = neighbor_discover[i];
      char* hex = &pubkey_hex[i * hex_size];
      mesh::Utils::toHex(hex, entry.id.pub_key, PUB_KEY_SIZE);
      entries[i].pubkey_hex = hex;
      entries[i].snr = entry.snr / 4.0f;
      bool heard_known = neighborHeardAgeUsable(entry.heard_timestamp, now_secs);
      entries[i].heard_unknown = !heard_known;
      entries[i].heard_secs_ago = heard_known ? (now_secs - entry.heard_timestamp) : 0;
      entries[i].scopes = entry.scopes;
      switch (entry.status) {
        case ND_RESPONDED:   entries[i].status = "responded"; break;
        case ND_SEND_FAILED: entries[i].status = "send_failed"; break;
        default:             entries[i].status = "timeout"; break;
      }
    }

    // insertion sort: most useful first (JSON builder drops the tail on overflow)
    for (int i = 1; i < publish_count; i++) {
      MQTTMessageBuilder::NeighborsMessageEntry entry = entries[i];
      int j = i;
      while (j > 0 && neighborPublishEntryComesBefore(entry, entries[j - 1])) {
        entries[j] = entries[j - 1];
        j--;
      }
      entries[j] = entry;
    }

#if defined(ESP_PLATFORM)
    NeighborsDocAllocator doc_alloc;
    JsonDocument doc(&doc_alloc);
#else
    JsonDocument doc;
#endif
    int json_len = MQTTMessageBuilder::buildNeighborsMessage(
      doc, neighbor_discover_origin, self_pubkey_hex, timestamp, self_scopes_buf,
      self_default_scope_buf, entries, publish_count,
      json_buf, MQTTBridge::NEIGHBORS_JSON_BUFFER_SIZE,
      neighbor_discover_count, neighbor_discover_queried_count,
      neighbor_discover_truncated);

    if (json_len > 0 && bridge) {
      bridge->requestPublishNeighbors(json_buf, (size_t)json_len);
    }
  }

  neighborsFree(scratch);
  neighborsFree(json_buf);

  neighbor_discover_active = false;
  neighbor_discover_count = 0;
  neighbor_discover_next = 0;
  neighbor_discover_publish_count = 0;
  neighbor_discover_queried_count = 0;
  neighbor_discover_json_size = 0;
  neighbor_discover_truncated = false;
  neighbor_discover_until = 0;
  neighbor_discover_request = NULL;
  if (_cli.getObserverPrefs()->mqtt_neighbors_enabled) {
    next_neighbors_publish = futureMillis(_cli.getObserverPrefs()->mqtt_neighbors_interval);
  }
}

// Advance the newest-first scope-query phase. Keep only one request in flight so
// its responder gets a clear reply opportunity and the packet pool stays free.
void MyMesh::loopNeighborDiscover() {
  if (!neighbor_discover_active) return;

  if (neighbor_discover_next >= neighbor_discover_count) {
    finishNeighborDiscover();
    return;
  }

  NeighborDiscoverEntry& entry = neighbor_discover[neighbor_discover_next];
  if (entry.status == ND_QUEUED) {
    if (!millisHasNowPassed(neighbor_discover_until)) return;
    if (cancelNeighborDiscoverRequest()) {
      entry.status = ND_SEND_FAILED;
      completeNeighborDiscoverEntry();
      return;
    }
    if (isCurrentOutbound(neighbor_discover_request)) {
      neighbor_discover_until = futureMillis(neighborDiscoverQueryTimeoutMs());
      return;
    }
    neighbor_discover_request = NULL;  // packet manager already shed it
    entry.status = ND_SEND_FAILED;
    completeNeighborDiscoverEntry();
    return;
  }
  if (entry.status == ND_PENDING) {
    if (!millisHasNowPassed(neighbor_discover_until)) return;
    entry.status = ND_TIMEOUT;
    completeNeighborDiscoverEntry();
    return;
  }
  if (entry.status == ND_RESPONDED || entry.status == ND_SEND_FAILED
      || entry.status == ND_TIMEOUT) {
    completeNeighborDiscoverEntry();
    return;
  }
  if (entry.status != ND_UNSENT) {
    neighbor_discover_next++;
    return;
  }

  uint32_t tag;
  mesh::Packet* request = sendAnonRegionsReq(entry.id, tag);
  if (request) {
    entry.tag = tag;
    entry.status = ND_QUEUED;
    neighbor_discover_request = request;
    neighbor_discover_until = futureMillis(NEIGHBOR_DISCOVER_QUEUE_TIMEOUT_MS);
  } else {
    entry.status = ND_SEND_FAILED;
    completeNeighborDiscoverEntry();
  }
}

// Shared precondition for starting a discovery: usable buffers + bridge running.
// PSRAM builds size their neighbors buffers for PSRAM, so a board whose PSRAM
// failed to init must not silently spend that much internal DRAM here.
// MQTT_NEIGHBORS_WITHOUT_PSRAM builds are already sized for internal DRAM.
bool MyMesh::neighborDiscoverReady(char* reply) {
#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
  if (!psramFound()) { strcpy(reply, "Err - PSRAM not available"); return false; }
#endif
  if (!bridge || !bridge->isRunning()) { strcpy(reply, "Err - MQTT bridge not running"); return false; }
  return true;
}

// Snapshot the neighbor table newest-first. loopNeighborDiscover() emits one
// anon-regions query at a time so hidden responders do not reply as a burst.
bool MyMesh::startNeighborDiscover(char* reply) {
  if (neighbor_discover_active) {
    strcpy(reply, "Err - neighbor discover already active");
    return false;
  }
  if (!neighborDiscoverReady(reply)) {
    return false;  // reply already set
  }

  neighbor_discover_count = 0;
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (neighbours[i].heard_timestamp > 0) {
      NeighborDiscoverEntry& entry = neighbor_discover[neighbor_discover_count];
      entry.id = neighbours[i].id;
      entry.heard_timestamp = neighbours[i].heard_timestamp;
      entry.snr = neighbours[i].snr;
      entry.scopes[0] = 0;
      entry.tag = 0;
      entry.status = ND_UNSENT;
      neighbor_discover_count++;
    }
  }

  // Query the freshest/strongest entries first; pubkey makes ties deterministic.
  for (int i = 1; i < neighbor_discover_count; i++) {
    NeighborDiscoverEntry entry = neighbor_discover[i];
    int j = i;
    while (j > 0) {
      auto& rhs = neighbor_discover[j - 1];
      bool before = entry.heard_timestamp > rhs.heard_timestamp
        || (entry.heard_timestamp == rhs.heard_timestamp && entry.snr > rhs.snr)
        || (entry.heard_timestamp == rhs.heard_timestamp && entry.snr == rhs.snr
            && memcmp(entry.id.pub_key, rhs.id.pub_key, PUB_KEY_SIZE) < 0);
      if (!before) break;
      neighbor_discover[j] = neighbor_discover[j - 1];
      j--;
    }
    neighbor_discover[j] = entry;
  }

  neighbor_discover_next = 0;
  resetNeighborDiscoverJsonBudget();
  neighbor_discover_active = true;
  neighbor_discover_until = 0;
  neighbor_discover_request = NULL;

  if (neighbor_discover_count == 0) {
    finishNeighborDiscover();
    strcpy(reply, "OK - neighbor discover started (0 neighbors, self only)");
  } else {
    loopNeighborDiscover();  // queue the first request now
    sprintf(reply, "OK - neighbor discover started (%u neighbors)", (unsigned)neighbor_discover_count);
  }
  return true;
}
#endif // WITH_MQTT_NEIGHBORS

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
#if defined(WITH_BRIDGE)
  if (bridge && bridge->isRunning()) return true;  // bridge needs WiFi radio, can't sleep
#endif
  return _mgr->getOutboundCount(0xFFFFFFFF) > 0;
}
