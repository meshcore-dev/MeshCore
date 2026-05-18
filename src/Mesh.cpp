#include "Mesh.h"
//#include <Arduino.h>

namespace mesh {

static const uint8_t DIRECT_RETRY_MAX_ATTEMPTS_DEFAULT = 15;
static const uint8_t DIRECT_RETRY_MAX_ATTEMPTS_HARD_MAX = 15;

static uint8_t decodeTraceHashSize(uint8_t flags, uint8_t route_bytes) {
  uint8_t code = flags & 0x03;
  uint8_t size_pow2 = (uint8_t)(1U << code);   // legacy TRACE interpretation
  uint8_t size_linear = (uint8_t)(code + 1U);  // packed-size interpretation (1..4)

  bool pow2_ok = size_pow2 > 0 && (route_bytes % size_pow2) == 0;
  bool linear_ok = size_linear > 0 && (route_bytes % size_linear) == 0;

  if (pow2_ok && !linear_ok) {
    return size_pow2;
  }
  if (linear_ok && !pow2_ok) {
    return size_linear;
  }
  if (pow2_ok) {
    return size_pow2;
  }
  return size_linear;
}

void Mesh::begin() {
  for (int i = 0; i < MAX_DIRECT_RETRY_SLOTS; i++) {
    _direct_retries[i].packet = NULL;
    _direct_retries[i].trigger_packet = NULL;
    _direct_retries[i].retry_started_at = 0;
    _direct_retries[i].echo_wait_started_at = 0;
    _direct_retries[i].retry_at = 0;
    _direct_retries[i].retry_delay = 0;
    _direct_retries[i].retry_attempts_sent = 0;
    _direct_retries[i].priority = 0;
    _direct_retries[i].progress_marker = 0;
    _direct_retries[i].expect_path_growth = false;
    _direct_retries[i].waiting_final_echo = false;
    _direct_retries[i].queued = false;
    _direct_retries[i].active = false;
  }
  Dispatcher::begin();
}

void Mesh::loop() {
  Dispatcher::loop();

  for (int i = 0; i < MAX_DIRECT_RETRY_SLOTS; i++) {
    if (!_direct_retries[i].active) {
      continue;
    }

    if (_direct_retries[i].waiting_final_echo) {
      if (!millisHasNowPassed(_direct_retries[i].retry_at)) {
        continue;
      }

      uint32_t elapsed_millis = _direct_retries[i].retry_started_at == 0
        ? 0
        : (uint32_t)(_ms->getMillis() - _direct_retries[i].retry_started_at);
      onDirectRetryEvent("failed_all_tries", _direct_retries[i].packet, elapsed_millis, _direct_retries[i].retry_attempts_sent);
      onDirectRetryEvent("failure", _direct_retries[i].packet, elapsed_millis, _direct_retries[i].retry_attempts_sent);
      clearDirectRetrySlot(i);
      continue;
    }

    if (!_direct_retries[i].queued || !millisHasNowPassed(_direct_retries[i].retry_at)) {
      continue;
    }

    if (!isDirectRetryQueued(_direct_retries[i].packet)) {
      if (_direct_retries[i].packet == getOutboundInFlight()) {
        continue;  // currently transmitting; keep slot until onSendComplete/onSendFail emits event
      }
      clearDirectRetrySlot(i);
    }
  }
}

bool Mesh::allowPacketForward(const mesh::Packet* packet) { 
  return false;  // by default, Transport NOT enabled
}
uint32_t Mesh::getRetransmitDelay(const mesh::Packet* packet) { 
  uint32_t t = (_radio->getEstAirtimeFor(packet->getRawLength()) * 52 / 50) / 2;

  return _rng->nextInt(0, 5)*t;
}
uint32_t Mesh::getDirectRetransmitDelay(const Packet* packet) {
  return 0;  // by default, no delay
}
bool Mesh::allowDirectRetry(const Packet* packet, const uint8_t* next_hop_hash, uint8_t next_hop_hash_len) const {
  return false;
}
uint32_t Mesh::getDirectRetryEchoDelay(const Packet* packet) const {
  // Keep the base fallback aligned with the repeater's minimum retry wait.
  return 200;
}
uint8_t Mesh::getDirectRetryMaxAttempts(const Packet* packet) const {
  return DIRECT_RETRY_MAX_ATTEMPTS_DEFAULT;
}
uint32_t Mesh::getDirectRetryAttemptDelay(const Packet* packet, uint8_t attempt_idx) {
  uint32_t base = getDirectRetryEchoDelay(packet);
  // Keep the historical linear spacing while allowing the base wait to vary by platform/profile.
  return base + ((uint32_t)attempt_idx * 100UL);
}
uint8_t Mesh::getExtraAckTransmitCount() const {
  return 0;
}

void Mesh::onSendComplete(Packet* packet) {
  armDirectRetryOnSendComplete(packet);
}

void Mesh::onSendFail(Packet* packet) {
  clearPendingDirectRetryOnSendFail(packet);
}

uint32_t Mesh::getCADFailRetryDelay() const {
  return _rng->nextInt(1, 4)*120;
}

int Mesh::searchPeersByHash(const uint8_t* hash) {
  return 0;  // not found
}

int Mesh::searchChannelsByHash(const uint8_t* hash, GroupChannel channels[], int max_matches) {
  return 0;  // not found
}

DispatcherAction Mesh::onRecvPacket(Packet* pkt) {
  if (pkt->isRouteDirect()) {
    cancelDirectRetryOnEcho(pkt);
  }

  if (pkt->isRouteDirect() && pkt->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    if (pkt->path_len < MAX_PATH_SIZE) {
      uint8_t i = 0;
      uint32_t trace_tag;
      memcpy(&trace_tag, &pkt->payload[i], 4); i += 4;
      uint32_t auth_code;
      memcpy(&auth_code, &pkt->payload[i], 4); i += 4;
      uint8_t flags = pkt->payload[i++];
      uint8_t len = pkt->payload_len - i;
      uint8_t hash_size = decodeTraceHashSize(flags, len);
      // path_len*entry_size can exceed 255 (path_len up to 63, entry_size up to 8);
      // a uint8_t offset would wrap and steer the isHashMatch() read to the wrong place.
      uint16_t offset = (uint16_t)pkt->path_len * (uint16_t)hash_size;
      if (offset >= len) {   // TRACE has reached end of given path
        onTraceRecv(pkt, trace_tag, auth_code, flags, pkt->path, &pkt->payload[i], len);
      } else if (hash_size > 0 && offset + hash_size <= len
          && self_id.isHashMatch(&pkt->payload[i + offset], hash_size)
          && allowPacketForward(pkt) && !_tables->hasSeen(pkt)) {
        // append SNR (Not hash!)
        pkt->path[pkt->path_len++] = (int8_t) (pkt->getSNR()*4);

        uint32_t d = getDirectRetransmitDelay(pkt);
        maybeScheduleDirectRetry(pkt, 5);
        return ACTION_RETRANSMIT_DELAYED(5, d);  // schedule with priority 5 (for now), maybe make configurable?
      }
    }
    return ACTION_RELEASE;
  }

  if (pkt->isRouteDirect() && pkt->getPayloadType() == PAYLOAD_TYPE_CONTROL && (pkt->payload[0] & 0x80) != 0) {
    if (pkt->getPathHashCount() == 0) {
      onControlDataRecv(pkt);
    }
    // just zero-hop control packets allowed (for this subset of payloads)
    return ACTION_RELEASE;
  }

  if (pkt->isRouteDirect() && pkt->getPathHashCount() > 0) {
    // check for 'early received' ACK
    if (pkt->getPayloadType() == PAYLOAD_TYPE_ACK) {
      int i = 0;
      uint32_t ack_crc;
      memcpy(&ack_crc, &pkt->payload[i], 4); i += 4;
      if (i <= pkt->payload_len) {
        onAckRecv(pkt, ack_crc);
      }
    }

    if (self_id.isHashMatch(pkt->path, pkt->getPathHashSize()) && allowPacketForward(pkt)) {
      if (pkt->getPayloadType() == PAYLOAD_TYPE_MULTIPART) {
        return forwardMultipartDirect(pkt);
      } else if (pkt->getPayloadType() == PAYLOAD_TYPE_ACK) {
        if (!_tables->hasSeen(pkt)) {  // don't retransmit!
          removeSelfFromPath(pkt);
          routeDirectRecvAcks(pkt, 0);
        }
        return ACTION_RELEASE;
      }

      if (!_tables->hasSeen(pkt)) {
        removeSelfFromPath(pkt);

        uint32_t d = getDirectRetransmitDelay(pkt);
        maybeScheduleDirectRetry(pkt, 0);
        return ACTION_RETRANSMIT_DELAYED(0, d);  // Routed traffic is HIGHEST priority 
      }
    }
    return ACTION_RELEASE;   // this node is NOT the next hop (OR this packet has already been forwarded), so discard.
  }

  if (pkt->isRouteFlood() && filterRecvFloodPacket(pkt)) return ACTION_RELEASE;

  DispatcherAction action = ACTION_RELEASE;

  switch (pkt->getPayloadType()) {
    case PAYLOAD_TYPE_ACK: {
      int i = 0;
      uint32_t ack_crc;
      memcpy(&ack_crc, &pkt->payload[i], 4); i += 4;
      if (i > pkt->payload_len) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete ACK packet", getLogDateTime());
      } else if (!_tables->hasSeen(pkt)) {
        onAckRecv(pkt, ack_crc);
        action = routeRecvPacket(pkt);
      }
      break;
    }
    case PAYLOAD_TYPE_PATH:
    case PAYLOAD_TYPE_REQ:
    case PAYLOAD_TYPE_RESPONSE:
    case PAYLOAD_TYPE_TXT_MSG: {
      int i = 0;
      uint8_t dest_hash = pkt->payload[i++];
      uint8_t src_hash = pkt->payload[i++];

      uint8_t* macAndData = &pkt->payload[i];   // MAC + encrypted data 
      if (i + CIPHER_MAC_SIZE >= pkt->payload_len) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete data packet", getLogDateTime());
      } else if (!_tables->hasSeen(pkt)) {
        // NOTE: this is a 'first packet wins' impl. When receiving from multiple paths, the first to arrive wins.
        //       For flood mode, the path may not be the 'best' in terms of hops.
        // FUTURE: could send back multiple paths, using createPathReturn(), and let sender choose which to use(?)

        if (self_id.isHashMatch(&dest_hash)) {
          // scan contacts DB, for all matching hashes of 'src_hash' (max 4 matches supported ATM)
          int num = searchPeersByHash(&src_hash);
          // for each matching contact, try to decrypt data
          bool found = false;
          for (int j = 0; j < num; j++) {
            uint8_t secret[PUB_KEY_SIZE];
            getPeerSharedSecret(secret, j);

            // decrypt, checking MAC is valid
            uint8_t data[MAX_PACKET_PAYLOAD];
            int len = Utils::MACThenDecrypt(secret, data, macAndData, pkt->payload_len - i);
            if (len > 0) {  // success!
              if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH) {
                int k = 0;
                uint8_t path_len = data[k++];
                uint8_t hash_size = (path_len >> 6) + 1;
                uint8_t hash_count = path_len & 63;
                uint8_t* path = &data[k]; k += hash_size*hash_count;
                uint8_t extra_type = data[k++] & 0x0F;   // upper 4 bits reserved for future use
                uint8_t* extra = &data[k];
                uint8_t extra_len = len - k;   // remainder of packet (may be padded with zeroes!)
                if (onPeerPathRecv(pkt, j, secret, path, path_len, extra_type, extra, extra_len)) {
                  if (pkt->isRouteFlood()) {
                    // send a reciprocal return path to sender, but send DIRECTLY!
                    mesh::Packet* rpath = createPathReturn(&src_hash, secret, pkt->path, pkt->path_len, 0, NULL, 0);
                    if (rpath) sendDirect(rpath, path, path_len, 500);
                  }
                }
              } else {
                onPeerDataRecv(pkt, pkt->getPayloadType(), j, secret, data, len);
              }
              found = true;
              break;
            }
          }
          if (found) {
            pkt->markDoNotRetransmit();  // packet was for this node, so don't retransmit
          } else {
            MESH_DEBUG_PRINTLN("%s recv matches no peers, src_hash=%02X", getLogDateTime(), (uint32_t)src_hash);
          }
        }
        action = routeRecvPacket(pkt);
      }
      break;
    }
    case PAYLOAD_TYPE_ANON_REQ: {
      int i = 0;
      uint8_t dest_hash = pkt->payload[i++];
      uint8_t* sender_pub_key = &pkt->payload[i]; i += PUB_KEY_SIZE;

      uint8_t* macAndData = &pkt->payload[i];   // MAC + encrypted data 
      if (i + 2 >= pkt->payload_len) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete data packet", getLogDateTime());
      } else if (!_tables->hasSeen(pkt)) {
        if (self_id.isHashMatch(&dest_hash)) {
          Identity sender(sender_pub_key);

          uint8_t secret[PUB_KEY_SIZE];
          self_id.calcSharedSecret(secret, sender);

          // decrypt, checking MAC is valid
          uint8_t data[MAX_PACKET_PAYLOAD];
          int len = Utils::MACThenDecrypt(secret, data, macAndData, pkt->payload_len - i);
          if (len > 0) {  // success!
            onAnonDataRecv(pkt, secret, sender, data, len);
            pkt->markDoNotRetransmit();
          }
        }
        action = routeRecvPacket(pkt);
      }
      break;
    }
    case PAYLOAD_TYPE_GRP_DATA: 
    case PAYLOAD_TYPE_GRP_TXT: {
      int i = 0;
      uint8_t channel_hash = pkt->payload[i++];

      uint8_t* macAndData = &pkt->payload[i];   // MAC + encrypted data 
      if (i + 2 >= pkt->payload_len) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete data packet", getLogDateTime());
      } else if (!_tables->hasSeen(pkt)) {
        // scan channels DB, for all matching hashes of 'channel_hash' (max 4 matches supported ATM)
        GroupChannel channels[4];
        int num = searchChannelsByHash(&channel_hash, channels, 4);
        // for each matching channel, try to decrypt data
        for (int j = 0; j < num; j++) {
          // decrypt, checking MAC is valid
          uint8_t data[MAX_PACKET_PAYLOAD];
          int len = Utils::MACThenDecrypt(channels[j].secret, data, macAndData, pkt->payload_len - i);
          if (len > 0) {  // success!
            onGroupDataRecv(pkt, pkt->getPayloadType(), channels[j], data, len);
            break;
          }
        }
        action = routeRecvPacket(pkt);
      }
      break;
    }
    case PAYLOAD_TYPE_ADVERT: {
      int i = 0;
      Identity id;
      memcpy(id.pub_key, &pkt->payload[i], PUB_KEY_SIZE); i += PUB_KEY_SIZE;

      uint32_t timestamp;
      memcpy(&timestamp, &pkt->payload[i], 4); i += 4;
      const uint8_t* signature = &pkt->payload[i]; i += SIGNATURE_SIZE;

      if (i > pkt->payload_len) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete advertisement packet", getLogDateTime());
      } else if (self_id.matches(id.pub_key)) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): receiving SELF advert packet", getLogDateTime());
      } else if (!_tables->hasSeen(pkt)) {
        uint8_t* app_data = &pkt->payload[i];
        int app_data_len = pkt->payload_len - i;
        if (app_data_len > MAX_ADVERT_DATA_SIZE) { app_data_len = MAX_ADVERT_DATA_SIZE; }

        // check that signature is valid
        bool is_ok;
        {
          uint8_t message[PUB_KEY_SIZE + 4 + MAX_ADVERT_DATA_SIZE];
          int msg_len = 0;
          memcpy(&message[msg_len], id.pub_key, PUB_KEY_SIZE); msg_len += PUB_KEY_SIZE;
          memcpy(&message[msg_len], &timestamp, 4); msg_len += 4;
          memcpy(&message[msg_len], app_data, app_data_len); msg_len += app_data_len;

          is_ok = id.verify(signature, message, msg_len);
        }
        if (is_ok) {
          MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): valid advertisement received!", getLogDateTime());
          onAdvertRecv(pkt, id, timestamp, app_data, app_data_len);
          action = routeRecvPacket(pkt);
        } else {
          MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): received advertisement with forged signature! (app_data_len=%d)", getLogDateTime(), app_data_len);
        }
      }
      break;
    }
    case PAYLOAD_TYPE_RAW_CUSTOM: {
      if (pkt->isRouteDirect() && !_tables->hasSeen(pkt)) {
        onRawDataRecv(pkt);
        //action = routeRecvPacket(pkt);    don't flood route these (yet)
      }
      break;
    }
    case PAYLOAD_TYPE_MULTIPART:
      if (pkt->payload_len > 2) {
        uint8_t remaining = pkt->payload[0] >> 4;  // num of packets in this multipart sequence still to be sent
        uint8_t type = pkt->payload[0] & 0x0F;

        if (type == PAYLOAD_TYPE_ACK && pkt->payload_len >= 5) {    // a multipart ACK
          Packet tmp;
          tmp.header = pkt->header;
          tmp.path_len = Packet::copyPath(tmp.path, pkt->path, pkt->path_len);
          tmp.payload_len = pkt->payload_len - 1;
          memcpy(tmp.payload, &pkt->payload[1], tmp.payload_len);

          if (!_tables->hasSeen(&tmp)) {
            uint32_t ack_crc;
            memcpy(&ack_crc, tmp.payload, 4);

            onAckRecv(&tmp, ack_crc);
            //action = routeRecvPacket(&tmp);  // NOTE: currently not needed, as multipart ACKs not sent Flood
          }
        } else {
          // FUTURE: other multipart types??
        }
      }
      break;

    default:
      MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): unknown payload type, header: %d", getLogDateTime(), (int) pkt->header);
      // Don't flood route unknown packet types!   action = routeRecvPacket(pkt);
      break;
  }
  return action;
}

void Mesh::removeSelfFromPath(Packet* pkt) {
  // remove our hash from 'path'
  pkt->setPathHashCount(pkt->getPathHashCount() - 1);  // decrement the count

  uint8_t sz = pkt->getPathHashSize();
  for (int k = 0; k < pkt->getPathHashCount()*sz; k += sz) {  // shuffle path by 1 'entry'
    memcpy(&pkt->path[k], &pkt->path[k + sz], sz);
  }
}

DispatcherAction Mesh::routeRecvPacket(Packet* packet) {
  uint8_t n = packet->getPathHashCount();
  if (packet->isRouteFlood() && !packet->isMarkedDoNotRetransmit()
    && (n + 1)*packet->getPathHashSize() <= MAX_PATH_SIZE && allowPacketForward(packet)) {
    // append this node's hash to 'path'
    self_id.copyHashTo(&packet->path[n * packet->getPathHashSize()], packet->getPathHashSize());
    packet->setPathHashCount(n + 1);

    uint32_t d = getRetransmitDelay(packet);
    // as this propagates outwards, give it lower and lower priority
    return ACTION_RETRANSMIT_DELAYED(packet->getPathHashCount(), d);   // give priority to closer sources, than ones further away
  }
  return ACTION_RELEASE;
}

DispatcherAction Mesh::forwardMultipartDirect(Packet* pkt) {
  uint8_t remaining = pkt->payload[0] >> 4;  // num of packets in this multipart sequence still to be sent
  uint8_t type = pkt->payload[0] & 0x0F;

  if (type == PAYLOAD_TYPE_ACK && pkt->payload_len >= 5) {    // a multipart ACK
    Packet tmp;
    tmp.header = pkt->header;
    tmp.path_len = Packet::copyPath(tmp.path, pkt->path, pkt->path_len);
    tmp.payload_len = pkt->payload_len - 1;
    memcpy(tmp.payload, &pkt->payload[1], tmp.payload_len);

    if (!_tables->hasSeen(&tmp)) {   // don't retransmit!
      removeSelfFromPath(&tmp);
      routeDirectRecvAcks(&tmp, ((uint32_t)remaining + 1) * 300);  // expect multipart ACKs 300ms apart (x2)
    }
  }
  return ACTION_RELEASE;
}

void Mesh::routeDirectRecvAcks(Packet* packet, uint32_t delay_millis) {
  if (!packet->isMarkedDoNotRetransmit()) {
    uint32_t crc;
    memcpy(&crc, packet->payload, 4);

    uint8_t extra = getExtraAckTransmitCount();
    while (extra > 0) {
      delay_millis += getDirectRetransmitDelay(packet) + 300;
      auto a1 = createMultiAck(crc, extra);
      if (a1) {
        a1->path_len = Packet::copyPath(a1->path, packet->path, packet->path_len);
        a1->header &= ~PH_ROUTE_MASK;
        a1->header |= ROUTE_TYPE_DIRECT;
        maybeScheduleDirectRetry(a1, 0);
        sendPacket(a1, 0, delay_millis);
      }
      extra--;
    }

    auto a2 = createAck(crc);
    if (a2) {
      a2->path_len = Packet::copyPath(a2->path, packet->path, packet->path_len);
      a2->header &= ~PH_ROUTE_MASK;
      a2->header |= ROUTE_TYPE_DIRECT;
      maybeScheduleDirectRetry(a2, 0);
      sendPacket(a2, 0, delay_millis);
    }
  }
}

void Mesh::clearDirectRetrySlot(int idx) {
  if (_direct_retries[idx].waiting_final_echo && _direct_retries[idx].packet != NULL) {
    releasePacket(_direct_retries[idx].packet);
  }
  _direct_retries[idx].packet = NULL;
  _direct_retries[idx].trigger_packet = NULL;
  _direct_retries[idx].retry_started_at = 0;
  _direct_retries[idx].echo_wait_started_at = 0;
  _direct_retries[idx].retry_at = 0;
  _direct_retries[idx].retry_delay = 0;
  _direct_retries[idx].retry_attempts_sent = 0;
  _direct_retries[idx].priority = 0;
  _direct_retries[idx].progress_marker = 0;
  _direct_retries[idx].expect_path_growth = false;
  _direct_retries[idx].waiting_final_echo = false;
  _direct_retries[idx].queued = false;
  _direct_retries[idx].active = false;
}

bool Mesh::isDirectRetryQueued(const Packet* packet) const {
  for (int i = 0; i < _mgr->getOutboundTotal(); i++) {
    if (_mgr->getOutboundByIdx(i) == packet) {
      return true;
    }
  }
  return false;
}

void Mesh::calculateDirectRetryKey(const Packet* packet, uint8_t* dest_key) const {
  uint8_t type = packet->getPayloadType();
  Utils::sha256(dest_key, MAX_HASH_SIZE, &type, 1, packet->payload, packet->payload_len);
}

bool Mesh::cancelDirectRetryOnEcho(const Packet* packet) {
  uint8_t recv_key[MAX_HASH_SIZE];
  calculateDirectRetryKey(packet, recv_key);

  bool cleared = false;
  for (int i = 0; i < MAX_DIRECT_RETRY_SLOTS; i++) {
    if (!_direct_retries[i].active || memcmp(recv_key, _direct_retries[i].retry_key, MAX_HASH_SIZE) != 0) {
      continue;
    }

    bool is_echo = _direct_retries[i].expect_path_growth
      ? packet->path_len > _direct_retries[i].progress_marker
      : packet->getPathHashCount() < _direct_retries[i].progress_marker;
    if (!is_echo) {
      continue;
    }

    int8_t echo_snr_x4 = packet->_snr;
    if (_direct_retries[i].queued || _direct_retries[i].waiting_final_echo) {
      if (_direct_retries[i].packet != NULL) {
        // Success quality comes from the received downstream echo, not the original upstream RX.
        _direct_retries[i].packet->_snr = echo_snr_x4;
      }
      uint32_t echo_millis = _direct_retries[i].echo_wait_started_at == 0
        ? 0
        : (uint32_t)(_ms->getMillis() - _direct_retries[i].echo_wait_started_at);
      uint8_t retry_attempt = _direct_retries[i].waiting_final_echo
        ? _direct_retries[i].retry_attempts_sent
        : _direct_retries[i].retry_attempts_sent + 1;
      onDirectRetryEvent("good", _direct_retries[i].packet, echo_millis, retry_attempt);
      if (_direct_retries[i].queued) {
        for (int j = 0; j < _mgr->getOutboundTotal(); j++) {
          if (_mgr->getOutboundByIdx(j) == _direct_retries[i].packet) {
            Packet* pending = _mgr->removeOutboundByIdx(j);
            if (pending) {
              releasePacket(pending);
            }
            break;
          }
        }
      }
      clearDirectRetrySlot(i);
    } else {
      if (_direct_retries[i].trigger_packet != NULL) {
        _direct_retries[i].trigger_packet->_snr = echo_snr_x4;
      }
      uint32_t echo_millis = _direct_retries[i].echo_wait_started_at == 0
        ? 0
        : (uint32_t)(_ms->getMillis() - _direct_retries[i].echo_wait_started_at);
      onDirectRetryEvent("good", _direct_retries[i].trigger_packet, echo_millis, _direct_retries[i].retry_attempts_sent + 1);
      clearDirectRetrySlot(i);
    }
    cleared = true;
  }

  return cleared;
}

void Mesh::armDirectRetryOnSendComplete(const Packet* packet) {
  for (int i = 0; i < MAX_DIRECT_RETRY_SLOTS; i++) {
    if (!_direct_retries[i].active) {
      continue;
    }

    if (_direct_retries[i].queued) {
      if (_direct_retries[i].packet == packet) {
        // The retry packet itself just finished transmitting; Dispatcher will release it after this hook.
        uint32_t elapsed_millis = _direct_retries[i].retry_started_at == 0
          ? 0
          : (uint32_t)(_ms->getMillis() - _direct_retries[i].retry_started_at);
        onDirectRetryEvent("resent", packet, elapsed_millis, _direct_retries[i].retry_attempts_sent + 1);
        _direct_retries[i].echo_wait_started_at = _ms->getMillis();
        _direct_retries[i].retry_attempts_sent++;
        uint8_t max_attempts = getDirectRetryMaxAttempts(packet);
        if (max_attempts < 1) {
          max_attempts = 1;
        } else if (max_attempts > DIRECT_RETRY_MAX_ATTEMPTS_HARD_MAX) {
          max_attempts = DIRECT_RETRY_MAX_ATTEMPTS_HARD_MAX;
        }
        if (_direct_retries[i].retry_attempts_sent >= max_attempts) {
          Packet* final_wait = obtainNewPacket();
          if (final_wait == NULL) {
            onDirectRetryEvent("dropped_no_packet", packet, elapsed_millis, _direct_retries[i].retry_attempts_sent);
            onDirectRetryEvent("failure", packet, elapsed_millis, _direct_retries[i].retry_attempts_sent);
            clearDirectRetrySlot(i);
            continue;
          }

          *final_wait = *packet;
          _direct_retries[i].packet = final_wait;
          _direct_retries[i].retry_at = futureMillis(_direct_retries[i].retry_delay);
          _direct_retries[i].waiting_final_echo = true;
          _direct_retries[i].queued = false;
          continue;
        }

        Packet* retry = obtainNewPacket();
        if (retry == NULL) {
          onDirectRetryEvent("dropped_no_packet", packet, elapsed_millis, _direct_retries[i].retry_attempts_sent + 1);
          onDirectRetryEvent("failure", packet, elapsed_millis, _direct_retries[i].retry_attempts_sent + 1);
          clearDirectRetrySlot(i);
          continue;
        }

        *retry = *packet;
        retry->tx_cr = 0;
        uint8_t retry_attempt = _direct_retries[i].retry_attempts_sent + 1;
        configureDirectRetryPacket(retry, packet, retry_attempt);
        uint32_t retry_delay = getDirectRetryAttemptDelay(packet, _direct_retries[i].retry_attempts_sent);
        sendPacket(retry, _direct_retries[i].priority, retry_delay);
        if (isDirectRetryQueued(retry)) {
          _direct_retries[i].packet = retry;
          _direct_retries[i].retry_delay = retry_delay;
          _direct_retries[i].retry_at = futureMillis(retry_delay);
          _direct_retries[i].waiting_final_echo = false;
          onDirectRetryEvent("queued", retry, retry_delay, retry_attempt);
        } else {
          onDirectRetryEvent("dropped_queue_full", retry, retry_delay, retry_attempt);
          onDirectRetryEvent("failure", retry, elapsed_millis, retry_attempt);
          clearDirectRetrySlot(i);
        }
      }
      continue;
    }

    if (_direct_retries[i].trigger_packet != packet) {
      continue;
    }

    // Allocate the retry packet only after TX-complete so busy repeaters do not reserve pool slots early.
    Packet* retry = obtainNewPacket();
    if (retry == NULL) {
      onDirectRetryEvent("dropped_no_packet", packet, _direct_retries[i].retry_delay, 1);
      onDirectRetryEvent("failure", packet, 0, 1);
      clearDirectRetrySlot(i);
      continue;
    }

    *retry = *packet;
    retry->tx_cr = 0;
    configureDirectRetryPacket(retry, packet, 1);

    // Start the echo wait only after the initial direct transmission actually completed.
    sendPacket(retry, _direct_retries[i].priority, _direct_retries[i].retry_delay);
    if (isDirectRetryQueued(retry)) {
      unsigned long now = _ms->getMillis();
      _direct_retries[i].packet = retry;
      _direct_retries[i].trigger_packet = NULL;
      _direct_retries[i].queued = true;
      _direct_retries[i].waiting_final_echo = false;
      _direct_retries[i].retry_at = futureMillis(_direct_retries[i].retry_delay);
      _direct_retries[i].retry_started_at = now;
      _direct_retries[i].echo_wait_started_at = now;
      onDirectRetryEvent("queued", retry, _direct_retries[i].retry_delay, 1);
    } else {
      onDirectRetryEvent("dropped_queue_full", retry, _direct_retries[i].retry_delay, 1);
      onDirectRetryEvent("failure", retry, 0, 1);
      clearDirectRetrySlot(i);
    }
  }
}

void Mesh::clearPendingDirectRetryOnSendFail(const Packet* packet) {
  for (int i = 0; i < MAX_DIRECT_RETRY_SLOTS; i++) {
    if (!_direct_retries[i].active) {
      continue;
    }

    if (_direct_retries[i].queued) {
      if (_direct_retries[i].packet == packet) {
        // The queued retry itself failed; Dispatcher will release it after this hook.
        onDirectRetryEvent("dropped_send_fail", packet, 0, _direct_retries[i].retry_attempts_sent + 1);
        onDirectRetryEvent("failure", packet, 0, _direct_retries[i].retry_attempts_sent + 1);
        clearDirectRetrySlot(i);
      }
      continue;
    }

    if (_direct_retries[i].trigger_packet == packet) {
      onDirectRetryEvent("dropped_send_fail", packet, 0, 1);
      onDirectRetryEvent("failure", packet, 0, 1);
      clearDirectRetrySlot(i);
    }
  }
}

bool Mesh::getDirectRetryTarget(const Packet* packet, const uint8_t*& next_hop_hash, uint8_t& next_hop_hash_len,
                                uint8_t& progress_marker, bool& expect_path_growth) const {
  switch (packet->getPayloadType()) {
    case PAYLOAD_TYPE_ACK:
    case PAYLOAD_TYPE_PATH:
    case PAYLOAD_TYPE_REQ:
    case PAYLOAD_TYPE_RESPONSE:
    case PAYLOAD_TYPE_TXT_MSG:
    case PAYLOAD_TYPE_ANON_REQ:
      // Allow retries even when only one downstream hop remains so fixed direct paths
      // (e.g. remote admin/login over 2-hop chains) use the same retry policy.
      if (packet->getPathHashCount() == 0) {
        return false;
      }
      next_hop_hash = packet->path;
      next_hop_hash_len = packet->getPathHashSize();
      progress_marker = packet->getPathHashCount();
      expect_path_growth = false;
      return true;

    case PAYLOAD_TYPE_MULTIPART:
      if (packet->payload_len < 1 || (packet->payload[0] & 0x0F) != PAYLOAD_TYPE_ACK || packet->getPathHashCount() == 0) {
        return false;
      }
      next_hop_hash = packet->path;
      next_hop_hash_len = packet->getPathHashSize();
      progress_marker = packet->getPathHashCount();
      expect_path_growth = false;
      return true;

    case PAYLOAD_TYPE_TRACE: {
      if (packet->payload_len < 9) {
        return false;
      }

      uint8_t route_bytes = packet->payload_len - 9;
      uint8_t hash_size = decodeTraceHashSize(packet->payload[8], route_bytes);
      uint16_t offset = (uint16_t)packet->path_len * (uint16_t)hash_size;
      if (offset + hash_size > route_bytes) {
        return false;
      }
      if (offset + (2 * hash_size) > route_bytes) {
        return false;  // no downstream repeater means there will be no forward echo to overhear.
      }

      next_hop_hash = &packet->payload[9 + offset];
      next_hop_hash_len = hash_size;
      progress_marker = packet->path_len;
      expect_path_growth = true;
      return true;
    }

    default:
      return false;
  }
}

void Mesh::maybeScheduleDirectRetry(const Packet* packet, uint8_t priority) {
  const uint8_t* next_hop_hash;
  uint8_t next_hop_hash_len;
  uint8_t progress_marker;
  bool expect_path_growth;
  if (!getDirectRetryTarget(packet, next_hop_hash, next_hop_hash_len, progress_marker, expect_path_growth)
      || !allowDirectRetry(packet, next_hop_hash, next_hop_hash_len)) {
    return;
  }

  int slot_idx = -1;
  for (int i = 0; i < MAX_DIRECT_RETRY_SLOTS; i++) {
    if (!_direct_retries[i].active) {
      slot_idx = i;
      break;
    }
  }
  if (slot_idx < 0) {
    onDirectRetryEvent("dropped_no_slot", packet, 0, 0);
    onDirectRetryEvent("failure", packet, 0, 0);
    return;
  }

  // Only store retry metadata here; allocate the retry packet after the initial TX really completes.
  uint32_t retry_delay = getDirectRetryAttemptDelay(packet, 0);
  calculateDirectRetryKey(packet, _direct_retries[slot_idx].retry_key);
  _direct_retries[slot_idx].packet = NULL;
  _direct_retries[slot_idx].trigger_packet = const_cast<Packet*>(packet);
  _direct_retries[slot_idx].retry_started_at = 0;
  _direct_retries[slot_idx].echo_wait_started_at = 0;
  _direct_retries[slot_idx].retry_at = 0;
  _direct_retries[slot_idx].retry_delay = retry_delay;
  _direct_retries[slot_idx].retry_attempts_sent = 0;
  _direct_retries[slot_idx].priority = priority;
  _direct_retries[slot_idx].progress_marker = progress_marker;
  _direct_retries[slot_idx].expect_path_growth = expect_path_growth;
  _direct_retries[slot_idx].waiting_final_echo = false;
  _direct_retries[slot_idx].queued = false;
  _direct_retries[slot_idx].active = true;
}

Packet* Mesh::createAdvert(const LocalIdentity& id, const uint8_t* app_data, size_t app_data_len) {
  if (app_data_len > MAX_ADVERT_DATA_SIZE) return NULL;

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createAdvert(): error, packet pool empty", getLogDateTime());
    return NULL;
  }

  packet->header = (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);  // ROUTE_TYPE_* is set later

  int len = 0;
  memcpy(&packet->payload[len], id.pub_key, PUB_KEY_SIZE); len += PUB_KEY_SIZE;

  uint32_t emitted_timestamp = _rtc->getCurrentTime();
  memcpy(&packet->payload[len], &emitted_timestamp, 4); len += 4;

  uint8_t* signature = &packet->payload[len]; len += SIGNATURE_SIZE;  // will fill this in later

  memcpy(&packet->payload[len], app_data, app_data_len); len += app_data_len;

  packet->payload_len = len;

  {
    uint8_t message[PUB_KEY_SIZE + 4 + MAX_ADVERT_DATA_SIZE];
    int msg_len = 0;
    memcpy(&message[msg_len], id.pub_key, PUB_KEY_SIZE); msg_len += PUB_KEY_SIZE;
    memcpy(&message[msg_len], &emitted_timestamp, 4); msg_len += 4;
    memcpy(&message[msg_len], app_data, app_data_len); msg_len += app_data_len;

    id.sign(signature, message, msg_len);
  }

  return packet;
}

#define MAX_COMBINED_PATH  (MAX_PACKET_PAYLOAD - 2 - CIPHER_BLOCK_SIZE)

Packet* Mesh::createPathReturn(const Identity& dest, const uint8_t* secret, const uint8_t* path, uint8_t path_len, uint8_t extra_type, const uint8_t*extra, size_t extra_len) {
  uint8_t dest_hash[PATH_HASH_SIZE];
  dest.copyHashTo(dest_hash);
  return createPathReturn(dest_hash, secret, path, path_len, extra_type, extra, extra_len);
}

Packet* Mesh::createPathReturn(const uint8_t* dest_hash, const uint8_t* secret, const uint8_t* path, uint8_t path_len, uint8_t extra_type, const uint8_t*extra, size_t extra_len) {
  uint8_t path_hash_size = (path_len >> 6) + 1;
  uint8_t path_hash_count = path_len & 63;

  if (path_hash_count*path_hash_size + extra_len + 5 > MAX_COMBINED_PATH) return NULL;  // too long!!

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createPathReturn(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  int len = 0;
  memcpy(&packet->payload[len], dest_hash, PATH_HASH_SIZE); len += PATH_HASH_SIZE;  // dest hash
  len += self_id.copyHashTo(&packet->payload[len]);  // src hash

  {
    int data_len = 0;
    uint8_t data[MAX_PACKET_PAYLOAD];

    data[data_len++] = path_len;
    memcpy(&data[data_len], path, path_hash_count*path_hash_size); data_len += path_hash_count*path_hash_size;
    if (extra_len > 0) {
      data[data_len++] = extra_type;
      memcpy(&data[data_len], extra, extra_len); data_len += extra_len;
    } else {
      // append a timestamp, or random blob (to make packet_hash unique)
      data[data_len++] = 0xFF;  // dummy payload type
      getRNG()->random(&data[data_len], 4); data_len += 4;
    }

    len += Utils::encryptThenMAC(secret, &packet->payload[len], data, data_len);
  }

  packet->payload_len = len;

  return packet;
}

Packet* Mesh::createDatagram(uint8_t type, const Identity& dest, const uint8_t* secret, const uint8_t* data, size_t data_len) {
  if (type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE) {
    if (data_len + CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE-1 > MAX_PACKET_PAYLOAD) return NULL;
  } else {
    return NULL;  // invalid type
  }

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createDatagram(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (type << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  int len = 0;
  len += dest.copyHashTo(&packet->payload[len]);  // dest hash
  len += self_id.copyHashTo(&packet->payload[len]);  // src hash
  len += Utils::encryptThenMAC(secret, &packet->payload[len], data, data_len);

  packet->payload_len = len;

  return packet;
}

Packet* Mesh::createAnonDatagram(uint8_t type, const LocalIdentity& sender, const Identity& dest, const uint8_t* secret, const uint8_t* data, size_t data_len) {
  if (type == PAYLOAD_TYPE_ANON_REQ) {
    if (data_len + 1 + PUB_KEY_SIZE + CIPHER_BLOCK_SIZE-1 > MAX_PACKET_PAYLOAD) return NULL;
  } else {
    return NULL;  // invalid type
  }

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createAnonDatagram(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (type << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  int len = 0;
  if (type == PAYLOAD_TYPE_ANON_REQ) {
    len += dest.copyHashTo(&packet->payload[len]);  // dest hash
    memcpy(&packet->payload[len], sender.pub_key, PUB_KEY_SIZE); len += PUB_KEY_SIZE;  // sender pub_key
  } else {
    // FUTURE:
  }
  len += Utils::encryptThenMAC(secret, &packet->payload[len], data, data_len);

  packet->payload_len = len;

  return packet;
}

Packet* Mesh::createGroupDatagram(uint8_t type, const GroupChannel& channel, const uint8_t* data, size_t data_len) {
  if (!(type == PAYLOAD_TYPE_GRP_TXT || type == PAYLOAD_TYPE_GRP_DATA)) return NULL;   // invalid type
  if (data_len + 1 + CIPHER_BLOCK_SIZE-1 > MAX_PACKET_PAYLOAD) return NULL; // too long

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createGroupDatagram(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (type << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  int len = 0;
  memcpy(&packet->payload[len], channel.hash, PATH_HASH_SIZE); len += PATH_HASH_SIZE;
  len += Utils::encryptThenMAC(channel.secret, &packet->payload[len], data, data_len);

  packet->payload_len = len;

  return packet;
}

Packet* Mesh::createAck(uint32_t ack_crc) {
  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createAck(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  memcpy(packet->payload, &ack_crc, 4);
  packet->payload_len = 4;

  return packet;
}

Packet* Mesh::createMultiAck(uint32_t ack_crc, uint8_t remaining) {
  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createMultiAck(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_MULTIPART << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  packet->payload[0] = (remaining << 4) | PAYLOAD_TYPE_ACK;
  memcpy(&packet->payload[1], &ack_crc, 4);
  packet->payload_len = 5;

  return packet;
}

Packet* Mesh::createRawData(const uint8_t* data, size_t len) {
  if (len > sizeof(Packet::payload)) return NULL;  // invalid arg

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createRawData(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  memcpy(packet->payload, data, len);
  packet->payload_len = len;

  return packet;
}

Packet* Mesh::createTrace(uint32_t tag, uint32_t auth_code, uint8_t flags) {
  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createTrace(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  memcpy(packet->payload, &tag, 4);
  memcpy(&packet->payload[4], &auth_code, 4);
  packet->payload[8] = flags;
  packet->payload_len = 9;  // NOTE: path will be appended to payload[] later

  return packet;
}

Packet* Mesh::createControlData(const uint8_t* data, size_t len) {
  if (len > sizeof(Packet::payload)) return NULL;  // invalid arg

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createControlData(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_CONTROL << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  memcpy(packet->payload, data, len);
  packet->payload_len = len;

  return packet;
}

void Mesh::sendFlood(Packet* packet, uint32_t delay_millis, uint8_t path_hash_size) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    MESH_DEBUG_PRINTLN("%s Mesh::sendFlood(): TRACE type not suspported", getLogDateTime());
    return;
  }
  if (path_hash_size == 0 || path_hash_size > 3) {
    MESH_DEBUG_PRINTLN("%s Mesh::sendFlood(): invalid path_hash_size", getLogDateTime());
    return;
  }

  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_FLOOD;
  packet->setPathHashSizeAndCount(path_hash_size, 0);

  _tables->markSent(packet); // mark this packet as already sent in case it is rebroadcast back to us

  uint8_t pri;
  if (packet->getPayloadType() == PAYLOAD_TYPE_PATH) {
    pri = 2;
  } else if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT) {
    pri = 3;   // de-prioritie these
  } else {
    pri = 1;
  }
  sendPacket(packet, pri, delay_millis);
}

void Mesh::sendFlood(Packet* packet, uint16_t* transport_codes, uint32_t delay_millis, uint8_t path_hash_size) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    MESH_DEBUG_PRINTLN("%s Mesh::sendFlood(): TRACE type not suspported", getLogDateTime());
    return;
  }
  if (path_hash_size == 0 || path_hash_size > 3) {
    MESH_DEBUG_PRINTLN("%s Mesh::sendFlood(): invalid path_hash_size", getLogDateTime());
    return;
  }

  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_TRANSPORT_FLOOD;
  packet->transport_codes[0] = transport_codes[0];
  packet->transport_codes[1] = transport_codes[1];
  packet->setPathHashSizeAndCount(path_hash_size, 0);

  _tables->markSent(packet); // mark this packet as already sent in case it is rebroadcast back to us

  uint8_t pri;
  if (packet->getPayloadType() == PAYLOAD_TYPE_PATH) {
    pri = 2;
  } else if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT) {
    pri = 3;   // de-prioritie these
  } else {
    pri = 1;
  }
  sendPacket(packet, pri, delay_millis);
}

void Mesh::sendDirect(Packet* packet, const uint8_t* path, uint8_t path_len, uint32_t delay_millis) {
  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_DIRECT;

  uint8_t pri;
  if (packet->getPayloadType() == PAYLOAD_TYPE_TRACE) {   // TRACE packets are different
    // for TRACE packets, path is appended to end of PAYLOAD. (path is used for SNR's)
    memcpy(&packet->payload[packet->payload_len], path, path_len);  // NOTE: path_len here can be > 64, and NOT in the new scheme
    packet->payload_len += path_len;

    packet->path_len = 0;
    pri = 5;   // maybe make this configurable
  } else {
    packet->path_len = Packet::copyPath(packet->path, path, path_len);
    if (packet->getPayloadType() == PAYLOAD_TYPE_PATH) {
      pri = 1;   // slightly less priority
    } else {
      pri = 0;
    }
  }
  _tables->markSent(packet); // mark this packet as already sent in case it is rebroadcast back to us
  maybeScheduleDirectRetry(packet, pri);
  sendPacket(packet, pri, delay_millis);
}

void Mesh::sendZeroHop(Packet* packet, uint32_t delay_millis) {
  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_DIRECT;

  packet->path_len = 0;  // path_len of zero means Zero Hop

  _tables->markSent(packet); // mark this packet as already sent in case it is rebroadcast back to us

  sendPacket(packet, 0, delay_millis);
}

void Mesh::sendZeroHop(Packet* packet, uint16_t* transport_codes, uint32_t delay_millis) {
  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_TRANSPORT_DIRECT;
  packet->transport_codes[0] = transport_codes[0];
  packet->transport_codes[1] = transport_codes[1];

  packet->path_len = 0;  // path_len of zero means Zero Hop

  _tables->markSent(packet); // mark this packet as already sent in case it is rebroadcast back to us

  sendPacket(packet, 0, delay_millis);
}

}
