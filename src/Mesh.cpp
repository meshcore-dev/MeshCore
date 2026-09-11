#include "Mesh.h"
//#include <Arduino.h>

namespace mesh {

void Mesh::begin() {
  Dispatcher::begin();
}

void Mesh::loop() {
  Dispatcher::loop();
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
uint8_t Mesh::getExtraAckTransmitCount() const {
  return 0;
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
  if (pkt->isRouteDirect() && pkt->getPayloadType() == PAYLOAD_TYPE_TRACE) {
    if (pkt->path_len < MAX_PATH_SIZE) {
      uint8_t i = 0;
      uint32_t trace_tag;
      memcpy(&trace_tag, &pkt->payload[i], 4); i += 4;
      uint32_t auth_code;
      memcpy(&auth_code, &pkt->payload[i], 4); i += 4;
      uint8_t flags = pkt->payload[i++];
      uint8_t path_sz = flags & 0x03;  // NEW v1.11+: lower 2 bits is path hash size

      uint8_t len = pkt->payload_len - i;
      // path_len*entry_size can exceed 255 (path_len up to 63, entry_size up to 8);
      // a uint8_t offset would wrap and steer the isHashMatch() read to the wrong place.
      uint16_t offset = (uint16_t)pkt->path_len << path_sz;
      if (offset >= len) {   // TRACE has reached end of given path
        onTraceRecv(pkt, trace_tag, auth_code, flags, pkt->path, &pkt->payload[i], len);
      } else if (self_id.isHashMatch(&pkt->payload[i + offset], 1 << path_sz) && allowPacketForward(pkt) && !_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
        // append SNR (Not hash!)
        pkt->path[pkt->path_len++] = (int8_t) (pkt->getSNR()*4);

        // Last forwarding hop before the TRACE destination: the destination does not forward,
        // so a retry could never be cancelled by overhearing a downstream forward. To avoid
        // flooding the destination, exhaust the retry budget now (no retransmissions from
        // this hop). Higher layers or the sender must retry the entire TRACE if needed.
        if (offset + (1 << path_sz) >= len) {
          pkt->sending_attempts = getMaxResendAttempts();
        }

        uint32_t d = getDirectRetransmitDelay(pkt);
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

  if (pkt->isRouteDirect()) {
    const bool is_next_hop = (pkt->getPathHashCount() > 0) && self_id.isHashMatch(pkt->path, pkt->getPathHashSize());

    // Only do this when the packet is NOT addressed to us as next hop to avoid dropping fresh packets.
    if (!is_next_hop) {
      // Check if this is a retransmit of a packet we recently sent;
      // if yes, the next hop successfully forwarded it, so remove our scheduled retransmit from outbound queue.
      // This runs for ANY path_len (including 0 = downstream relay forwarding to final destination),
      // so that we cancel pending retries as soon as we overhear a relay forwarding our packet.
      //
      // For TRACE packets we cannot use the packet hash, because TRACE appends SNR bytes
      // and increments path_len per hop, which changes the hash. Packet::isRetryMatch()
      // handles this by comparing payload and SNR prefix for TRACE, and hash for all others.

      // First check the current outbound packet being prepared/sent
      if (outbound && outbound->sending_attempts > 0 && outbound->isRouteDirect()) {
        if (pkt->isRetryMatch(outbound)) {
          MESH_DEBUG_PRINTLN(
              "%s Mesh::onRecvPacket(): downstream forwarded current outbound, canceling (attempt=%d)",
              getLogDateTime(), outbound->sending_attempts);
          releasePacket(outbound);
          outbound = NULL;
          return ACTION_RELEASE;
        }
      }

      if (_mgr->getOutboundTotal() > 0) {
        for (int i = _mgr->getOutboundTotal() - 1; i >= 0; i--) {
          Packet *queued_pkt = _mgr->getOutboundByIdx(i);
          if (queued_pkt && queued_pkt->sending_attempts > 0 && queued_pkt->isRouteDirect()) {
            if (pkt->isRetryMatch(queued_pkt)) {
              MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): downstream forwarded packet detected, canceling "
                                 "retransmit (attempt=%d)",
                                 getLogDateTime(), queued_pkt->sending_attempts);
              Packet *removed = _mgr->removeOutboundByIdx(i);
              if (removed) _mgr->free(removed);
              return ACTION_RELEASE; // don't process further: confirmed successful forwarding
            }
          }
        }
      }

    }

    // For direct packets with no relay hashes (zero-hop, addressed straight to us),
    // no further path-based processing applies.
    // A destination's receipt-ACK for a TXT_MSG we delivered as the final relay hop arrives
    // exactly this way: zero-path direct, because the destination ACKs its immediate
    // neighbour (it has no usable multi-hop return path to the originator, so the ACK only
    // travels one hop). The ACK-relay branch below is gated on is_next_hop, which requires
    // getPathHashCount() > 0 — unreachable for such an ACK. Cancel any pending final-hop
    // resend here, before the early-exit, so the resend does not fire after a delivery the
    // destination has already acknowledged.
    if (pkt->getPathHashCount() == 0) {
      if (pkt->getPayloadType() == PAYLOAD_TYPE_ACK) {
        cancelPendingFinalHopResend();
      }
      goto direct_path_done;
    }

    // check for 'early received' ACK
    if (pkt->getPayloadType() == PAYLOAD_TYPE_ACK) {
      int i = 0;
      uint32_t ack_crc;
      memcpy(&ack_crc, &pkt->payload[i], 4); i += 4;
      if (i <= pkt->payload_len) {
        onAckRecv(pkt, ack_crc);
      }
    }

    if (is_next_hop && allowPacketForward(pkt)) {
      if (pkt->getPayloadType() == PAYLOAD_TYPE_MULTIPART) {
        return forwardMultipartDirect(pkt);
      } else if (pkt->getPayloadType() == PAYLOAD_TYPE_ACK) {
        // This ACK is addressed back along the return path with us as the next hop, i.e. it
        // transits back through the very relay that delivered the original message. That is
        // proof the destination received it: cancel any pending final-hop resend so we do not
        // needlessly retransmit to the destination.
        cancelPendingFinalHopResend();

        if (!_tables->wasSeen(pkt)) {  // don't retransmit!
          _tables->markSeen(pkt);
          removeSelfFromPath(pkt);
          routeDirectRecvAcks(pkt, 0);
        }
        return ACTION_RELEASE;
      }

      if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);

        // Detect the final relay hop: only our own hash remained in the path, so after
        // removeSelfFromPath() the destination is reached implicitly (it lives in the payload
        // dest_hash, not as a relay hash) and getPathHashCount() becomes 0. The normal resend
        // guard (getPathHashCount() > 0) would then suppress any retransmit, leaving a lost
        // final-hop TX unrecoverable at the relay layer. TXT_MSG destinations ACK receipt, so
        // mark this packet to allow exactly one ACK-cancellable resend (see resendPacket() and
        // cancelPendingFinalHopResend()): the resend self-cancels on the returning ACK, so the
        // destination is not flooded.
        if (pkt->getPathHashCount() == 1 && pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
          pkt->final_hop_ack_resend = true;
        }

        removeSelfFromPath(pkt);

        MESH_DEBUG_PRINTLN("Mesh::onRecvPacket(): prepare to repeat packet %s", pkt->getHashHex());

        uint32_t d = getDirectRetransmitDelay(pkt);
        return ACTION_RETRANSMIT_DELAYED(0, d);  // Routed traffic is HIGHEST priority
      }
    }
    return ACTION_RELEASE;   // this node is NOT the next hop (OR this packet has already been forwarded), so discard.
  }

direct_path_done:
  if (pkt->isRouteFlood() && filterRecvFloodPacket(pkt)) return ACTION_RELEASE;

  DispatcherAction action = ACTION_RELEASE;

  switch (pkt->getPayloadType()) {
    case PAYLOAD_TYPE_ACK: {
      int i = 0;
      uint32_t ack_crc;
      memcpy(&ack_crc, &pkt->payload[i], 4); i += 4;
      if (i > pkt->payload_len) {
        MESH_DEBUG_PRINTLN("%s Mesh::onRecvPacket(): incomplete ACK packet", getLogDateTime());
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
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
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
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
                if (!Packet::isValidPathLen(path_len)) {
                  MESH_DEBUG_PRINTLN("%s PAYLOAD_TYPE_PATH, bad path_len: %u", getLogDateTime(), (uint32_t)path_len);
                  break;   // reject bad encoding
                }
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
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
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
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
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
      } else if (!_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
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
      if (pkt->isRouteDirect() && !_tables->wasSeen(pkt)) {
        _tables->markSeen(pkt);
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

          if (!_tables->wasSeen(&tmp)) {
            _tables->markSeen(&tmp);
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

void Mesh::cancelPendingFinalHopResend() {
  // The destination has ACKed receipt. ACKs are produced in delivery order and (for the
  // direct zero-path ACKs a companion sends) travel only one hop back to us, so the OLDEST
  // pending final-hop resend corresponds to the message this ACK acknowledges (FIFO). Cancel
  // exactly one — the earliest still-queued resend.
  //
  // The sending_attempts > 0 guard is essential: it selects only actual resends (the attempt
  // counter is bumped in resendPacket() before re-queueing) and never the original final-hop
  // forward, which is also flagged final_hop_ack_resend while it sits in the queue waiting to
  // be TXed. Without it, an ACK arriving in that brief pre-TX window would cancel the delivery
  // itself. (send_queue.add() appends, so index 0 is the oldest entry.)
  //
  // An in-flight resend (the 'outbound' packet, already on-air) is deliberately NOT touched:
  // aborting a mid-TX send risks radio state, and the duplicate it delivers is harmless (the
  // destination dedups via wasSeen). Only queued, not-yet-sent resends are cancelled.
  for (int i = 0; i < _mgr->getOutboundTotal(); i++) {
    Packet* queued = _mgr->getOutboundByIdx(i);
    if (queued && queued->final_hop_ack_resend && queued->sending_attempts > 0 &&
        queued->isRouteDirect() && queued->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
      MESH_DEBUG_PRINTLN("%s Mesh::cancelPendingFinalHopResend(): ACK heard, canceling oldest "
                         "final-hop resend (queued) %s", getLogDateTime(), queued->getHashHex());
      Packet* removed = _mgr->removeOutboundByIdx(i);
      if (removed) _mgr->free(removed);
      return;
    }
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

    if (!_tables->wasSeen(&tmp)) {   // don't retransmit!
      _tables->markSeen(&tmp);
      removeSelfFromPath(&tmp);
      routeDirectRecvAcks(&tmp, ((uint32_t)remaining + 1) * 300);  // expect multipart ACKs 300ms apart (x2)
    }
  }
  return ACTION_RELEASE;
}

void Mesh::routeDirectRecvAcks(Packet* packet, uint32_t delay_millis) {
  if (!packet->isMarkedDoNotRetransmit()) {
    uint8_t extra = getExtraAckTransmitCount();
    while (extra > 0) {
      delay_millis += getDirectRetransmitDelay(packet) + 300;
      auto a1 = createMultiAck(packet->payload, packet->payload_len, extra);
      if (a1) {
        a1->path_len = Packet::copyPath(a1->path, packet->path, packet->path_len);
        a1->header &= ~PH_ROUTE_MASK;
        a1->header |= ROUTE_TYPE_DIRECT;
        sendPacket(a1, 0, delay_millis);
      }
      extra--;
    }

    auto a2 = createAck(packet->payload, packet->payload_len);
    if (a2) {
      a2->path_len = Packet::copyPath(a2->path, packet->path, packet->path_len);
      a2->header &= ~PH_ROUTE_MASK;
      a2->header |= ROUTE_TYPE_DIRECT;
      sendPacket(a2, 0, delay_millis);
    }
  }
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

Packet* Mesh::createAck(const uint8_t* ack, uint8_t len) {
  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createAck(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  memcpy(packet->payload, ack, len);
  packet->payload_len = len;

  return packet;
}

Packet* Mesh::createMultiAck(const uint8_t* ack, uint8_t len, uint8_t remaining) {
  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    MESH_DEBUG_PRINTLN("%s Mesh::createMultiAck(): error, packet pool empty", getLogDateTime());
    return NULL;
  }
  packet->header = (PAYLOAD_TYPE_MULTIPART << PH_TYPE_SHIFT);  // ROUTE_TYPE_* set later

  packet->payload[0] = (remaining << 4) | PAYLOAD_TYPE_ACK;
  memcpy(&packet->payload[1], ack, len);
  packet->payload_len = 1 + len;

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

  _tables->markSeen(packet); // mark this packet as already sent in case it is rebroadcast back to us

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

  _tables->markSeen(packet); // mark this packet as already sent in case it is rebroadcast back to us

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
  _tables->markSeen(packet); // mark this packet as already sent in case it is rebroadcast back to us
  sendPacket(packet, pri, delay_millis);
}

void Mesh::sendZeroHop(Packet* packet, uint32_t delay_millis) {
  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_DIRECT;

  packet->path_len = 0;  // path_len of zero means Zero Hop

  _tables->markSeen(packet); // mark this packet as already sent in case it is rebroadcast back to us

  sendPacket(packet, 0, delay_millis);
}

void Mesh::sendZeroHop(Packet* packet, uint16_t* transport_codes, uint32_t delay_millis) {
  packet->header &= ~PH_ROUTE_MASK;
  packet->header |= ROUTE_TYPE_TRANSPORT_DIRECT;
  packet->transport_codes[0] = transport_codes[0];
  packet->transport_codes[1] = transport_codes[1];

  packet->path_len = 0;  // path_len of zero means Zero Hop

  _tables->markSeen(packet); // mark this packet as already sent in case it is rebroadcast back to us

  sendPacket(packet, 0, delay_millis);
}

}