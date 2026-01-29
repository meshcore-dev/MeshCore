#include <helpers/BaseChatMesh.h>
#include <Utils.h>

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY   300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY     200
#endif

void BaseChatMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  sendFlood(pkt, delay_millis);
}
void BaseChatMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  sendFlood(pkt, delay_millis);
}

mesh::Packet* BaseChatMesh::createSelfAdvert(const char* name) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len;
  {
    AdvertDataBuilder builder(ADV_TYPE_CHAT, name);
    app_data_len = builder.encodeTo(app_data);
  }

  return createAdvert(self_id, app_data, app_data_len);
}

mesh::Packet* BaseChatMesh::createSelfAdvert(const char* name, double lat, double lon) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len;
  {
    AdvertDataBuilder builder(ADV_TYPE_CHAT, name, lat, lon);
    app_data_len = builder.encodeTo(app_data);
  }

  return createAdvert(self_id, app_data, app_data_len);
}

void BaseChatMesh::sendAckTo(const ContactInfo& dest, uint32_t ack_hash) {
  if (dest.out_path_len < 0) {
    mesh::Packet* ack = createAck(ack_hash);
    if (ack) sendFloodScoped(dest, ack, TXT_ACK_DELAY);
  } else {
    uint32_t d = TXT_ACK_DELAY;
    if (getExtraAckTransmitCount() > 0) {
      mesh::Packet* a1 = createMultiAck(ack_hash, 1);
      if (a1) sendDirect(a1, dest.out_path, dest.out_path_len, d);
      d += 300;
    }

    mesh::Packet* a2 = createAck(ack_hash);
    if (a2) sendDirect(a2, dest.out_path, dest.out_path_len, d);
  }
}

void BaseChatMesh::bootstrapRTCfromContacts() {
  uint32_t latest = 0;
  for (int i = 0; i < num_contacts; i++) {
    if (contacts[i].lastmod > latest) {
      latest = contacts[i].lastmod;
    }
  }
  if (latest != 0) {
    getRTCClock()->setCurrentTime(latest + 1);
  }
}

ContactInfo* BaseChatMesh::allocateContactSlot() {
  if (num_contacts < MAX_CONTACTS) {
    return &contacts[num_contacts++];
  } else if (shouldOverwriteWhenFull()) {
    // Find oldest non-favourite contact by oldest lastmod timestamp
    int oldest_idx = -1;
    uint32_t oldest_lastmod = 0xFFFFFFFF;
    for (int i = 0; i < num_contacts; i++) {
      bool is_favourite = (contacts[i].flags & 0x01) != 0;
      if (!is_favourite && contacts[i].lastmod < oldest_lastmod) {
        oldest_lastmod = contacts[i].lastmod;
        oldest_idx = i;
      }
    }
    if (oldest_idx >= 0) {
      onContactOverwrite(contacts[oldest_idx].id.pub_key);
      return &contacts[oldest_idx];
    }
  }
  return NULL; // no space, no overwrite or all contacts are all favourites
}

void BaseChatMesh::populateContactFromAdvert(ContactInfo& ci, const mesh::Identity& id, const AdvertDataParser& parser, uint32_t timestamp) {
  memset(&ci, 0, sizeof(ci));
  ci.id = id;
  ci.out_path_len = -1;  // initially out_path is unknown
  StrHelper::strncpy(ci.name, parser.getName(), sizeof(ci.name));
  ci.type = parser.getType();
  if (parser.hasLatLon()) {
    ci.gps_lat = parser.getIntLat();
    ci.gps_lon = parser.getIntLon();
  }
  ci.last_advert_timestamp = timestamp;
  ci.lastmod = getRTCClock()->getCurrentTime();
}

void BaseChatMesh::onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) {
  AdvertDataParser parser(app_data, app_data_len);
  if (!(parser.isValid() && parser.hasName())) {
    MESH_DEBUG_PRINTLN("onAdvertRecv: invalid app_data, or name is missing: len=%d", app_data_len);
    return;
  }

  ContactInfo* from = NULL;
  for (int i = 0; i < num_contacts; i++) {
    if (id.matches(contacts[i].id)) {  // is from one of our contacts
      from = &contacts[i];
      if (timestamp <= from->last_advert_timestamp) {  // check for replay attacks!!
        MESH_DEBUG_PRINTLN("onAdvertRecv: Possible replay attack, name: %s", from->name);
        return;
      }
      break;
    }
  }

  // save a copy of raw advert packet (to support "Share..." function)
  int plen;
  {
    uint8_t save = packet->header;
    packet->header &= ~PH_ROUTE_MASK;
    packet->header |= ROUTE_TYPE_FLOOD;   // make sure transport codes are NOT saved
    plen = packet->writeTo(temp_buf);
    packet->header = save;
  }
  putBlobByKey(id.pub_key, PUB_KEY_SIZE, temp_buf, plen);

  bool is_new = false; // true = not in contacts[], false = exists in contacts[]
  if (from == NULL) {
    if (!shouldAutoAddContactType(parser.getType())) {
      ContactInfo ci;
      populateContactFromAdvert(ci, id, parser, timestamp);
      onDiscoveredContact(ci, true, packet->path_len, packet->path);       // let UI know
      return;
    }

    from = allocateContactSlot();
    if (from == NULL) {
      ContactInfo ci;
      populateContactFromAdvert(ci, id, parser, timestamp);
      onDiscoveredContact(ci, true, packet->path_len, packet->path);
      onContactsFull();
      MESH_DEBUG_PRINTLN("onAdvertRecv: unable to allocate contact slot for new contact");
      return;
    }
    
    populateContactFromAdvert(*from, id, parser, timestamp);
    from->sync_since = 0;
    from->shared_secret_valid = false;
  }
  // update
    StrHelper::strncpy(from->name, parser.getName(), sizeof(from->name));
    from->type = parser.getType();
    if (parser.hasLatLon()) {
      from->gps_lat = parser.getIntLat();
      from->gps_lon = parser.getIntLon();
    }
    from->last_advert_timestamp = timestamp;
    from->lastmod = getRTCClock()->getCurrentTime();

  onDiscoveredContact(*from, is_new, packet->path_len, packet->path);       // let UI know
}

int BaseChatMesh::searchPeersByHash(const uint8_t* hash) {
  int n = 0;
  for (int i = 0; i < num_contacts && n < MAX_SEARCH_RESULTS; i++) {
    if (contacts[i].id.isHashMatch(hash)) {
      matching_peer_indexes[n++] = i;  // store the INDEXES of matching contacts (for subsequent 'peer' methods)
    }
  }
  return n;
}

void BaseChatMesh::getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < num_contacts) {
    memcpy(dest_secret, contacts[i].getSharedSecret(self_id), PUB_KEY_SIZE);
  } else {
    MESH_DEBUG_PRINTLN("getPeerSharedSecret: Invalid peer idx: %d", i);
  }
}

void BaseChatMesh::onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) {
  int i = matching_peer_indexes[sender_idx];
  if (i < 0 || i >= num_contacts) {
    MESH_DEBUG_PRINTLN("onPeerDataRecv: Invalid sender idx: %d", i);
    return;
  }

  ContactInfo& from = contacts[i];

  if (type == PAYLOAD_TYPE_TXT_MSG && len > 5) {
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);  // timestamp (by sender's RTC clock - which could be wrong)
    uint8_t flags = data[4] >> 2;   // message attempt number, and other flags

    // len can be > original length, but 'text' will be padded with zeroes
    data[len] = 0; // need to make a C string again, with null terminator

    if (flags == TXT_TYPE_PLAIN) {
      from.lastmod = getRTCClock()->getCurrentTime(); // update last heard time
      onMessageRecv(from, packet, timestamp, (const char *) &data[5]);  // let UI know

      uint32_t ack_hash;    // calc truncated hash of the message timestamp + text + sender pub_key, to prove to sender that we got it
      mesh::Utils::sha256((uint8_t *) &ack_hash, 4, data, 5 + strlen((char *)&data[5]), from.id.pub_key, PUB_KEY_SIZE);

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the ACK
        mesh::Packet* path = createPathReturn(from.id, secret, packet->path, packet->path_len,
                                                PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 4);
        if (path) sendFloodScoped(from, path, TXT_ACK_DELAY);
      } else {
        sendAckTo(from, ack_hash);
      }
    } else if (flags == TXT_TYPE_CLI_DATA) {
      onCommandDataRecv(from, packet, timestamp, (const char *) &data[5]);  // let UI know
      // NOTE: no ack expected for CLI_DATA replies

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect() (NOTE: no ACK as extra)
        mesh::Packet* path = createPathReturn(from.id, secret, packet->path, packet->path_len, 0, NULL, 0);
        if (path) sendFloodScoped(from, path);
      }
    } else if (flags == TXT_TYPE_SIGNED_PLAIN) {
      if (timestamp > from.sync_since) {  // make sure 'sync_since' is up-to-date
        from.sync_since = timestamp;
      }
      from.lastmod = getRTCClock()->getCurrentTime(); // update last heard time
      onSignedMessageRecv(from, packet, timestamp, &data[5], (const char *) &data[9]);  // let UI know

      uint32_t ack_hash;    // calc truncated hash of the message timestamp + text + OUR pub_key, to prove to sender that we got it
      mesh::Utils::sha256((uint8_t *) &ack_hash, 4, data, 9 + strlen((char *)&data[9]), self_id.pub_key, PUB_KEY_SIZE);

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the ACK
        mesh::Packet* path = createPathReturn(from.id, secret, packet->path, packet->path_len,
                                                PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 4);
        if (path) sendFloodScoped(from, path, TXT_ACK_DELAY);
      } else {
        sendAckTo(from, ack_hash);
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported message type: %u", (uint32_t) flags);
    }
  } else if (type == PAYLOAD_TYPE_REQ && len > 4) {
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4);
    uint8_t reply_len = onContactRequest(from, sender_timestamp, &data[4], len - 4, temp_buf);
    if (reply_len > 0) {
      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
        mesh::Packet* path = createPathReturn(from.id, secret, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, temp_buf, reply_len);
        if (path) sendFloodScoped(from, path, SERVER_RESPONSE_DELAY);
      } else {
        mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, from.id, secret, temp_buf, reply_len);
        if (reply) {
          if (from.out_path_len >= 0) {  // we have an out_path, so send DIRECT
            sendDirect(reply, from.out_path, from.out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFloodScoped(from, reply, SERVER_RESPONSE_DELAY);
          }
        }
      }
    }
  } else if (type == PAYLOAD_TYPE_RESPONSE && len > 0) {
    onContactResponse(from, data, len);
    if (packet->isRouteFlood() && from.out_path_len >= 0) {
      // we have direct path, but other node is still sending flood response, so maybe they didn't receive reciprocal path properly(?)
      handleReturnPathRetry(from, packet->path, packet->path_len);
    }
  }
}

bool BaseChatMesh::onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  int i = matching_peer_indexes[sender_idx];
  if (i < 0 || i >= num_contacts) {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: Invalid sender idx: %d", i);
    return false;
  }

  ContactInfo& from = contacts[i];

  return onContactPathRecv(from, packet->path, packet->path_len, path, path_len, extra_type, extra, extra_len);
}

bool BaseChatMesh::onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  // NOTE: default impl, we just replace the current 'out_path' regardless, whenever sender sends us a new out_path.
  // FUTURE: could store multiple out_paths per contact, and try to find which is the 'best'(?)
  memcpy(from.out_path, out_path, from.out_path_len = out_path_len);  // store a copy of path, for sendDirect()
  from.lastmod = getRTCClock()->getCurrentTime();

  onContactPathUpdated(from);

  if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4) {
    // also got an encoded ACK!
    if (processAck(extra) != NULL) {
      txt_send_timeout = 0;   // matched one we're waiting for, cancel timeout timer
    }
  } else if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 0) {
    onContactResponse(from, extra, extra_len);
  }
  return true;  // send reciprocal path if necessary
}

void BaseChatMesh::onAckRecv(mesh::Packet* packet, uint32_t ack_crc) {
  ContactInfo* from;
  if ((from = processAck((uint8_t *)&ack_crc)) != NULL) {
    txt_send_timeout = 0;   // matched one we're waiting for, cancel timeout timer
    packet->markDoNotRetransmit();   // ACK was for this node, so don't retransmit

    if (packet->isRouteFlood() && from->out_path_len >= 0) {
      // we have direct path, but other node is still sending flood, so maybe they didn't receive reciprocal path properly(?)
      handleReturnPathRetry(*from, packet->path, packet->path_len);
    }
  }
}

void BaseChatMesh::handleReturnPathRetry(const ContactInfo& contact, const uint8_t* path, uint8_t path_len) {
  // NOTE: simplest impl is just to re-send a reciprocal return path to sender (DIRECTLY)
  //        override this method in various firmwares, if there's a better strategy
  mesh::Packet* rpath = createPathReturn(contact.id, contact.getSharedSecret(self_id), path, path_len, 0, NULL, 0);
  if (rpath) sendDirect(rpath, contact.out_path, contact.out_path_len, 3000);   // 3 second delay
}

#ifdef MAX_GROUP_CHANNELS
int BaseChatMesh::searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel dest[], int max_matches) {
  int n = 0;
  for (int i = 0; i < MAX_GROUP_CHANNELS && n < max_matches; i++) {
    if (channels[i].channel.hash[0] == hash[0]) {
      dest[n++] = channels[i].channel;
    }
  }
  return n;
}
#endif

void BaseChatMesh::onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) {
  uint8_t txt_type = data[4];
  if (type == PAYLOAD_TYPE_GRP_TXT && len > 5 && (txt_type >> 2) == 0) {  // 0 = plain text msg
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    // len can be > original length, but 'text' will be padded with zeroes
    data[len] = 0; // need to make a C string again, with null terminator

    // notify UI  of this new message
    onChannelMessageRecv(channel, packet, timestamp, (const char *) &data[5]);  // let UI know
  }
}

mesh::Packet* BaseChatMesh::composeMsgPacket(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char *text, uint32_t& expected_ack) {
  int text_len = strlen(text);
  if (text_len > MAX_TEXT_LEN) return NULL;
  if (attempt > 3 && text_len > MAX_TEXT_LEN-2) return NULL;

  uint8_t temp[5+MAX_TEXT_LEN+1];
  memcpy(temp, &timestamp, 4);   // mostly an extra blob to help make packet_hash unique
  temp[4] = (attempt & 3);
  memcpy(&temp[5], text, text_len + 1);

  // calc expected ACK reply
  mesh::Utils::sha256((uint8_t *)&expected_ack, 4, temp, 5 + text_len, self_id.pub_key, PUB_KEY_SIZE);

  int len = 5 + text_len;
  if (attempt > 3) {
    temp[len++] = 0;  // null terminator
    temp[len++] = attempt;  // hide attempt number at tail end of payload
  }

  return createDatagram(PAYLOAD_TYPE_TXT_MSG, recipient.id, recipient.getSharedSecret(self_id), temp, len);
}

int  BaseChatMesh::sendMessage(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text, uint32_t& expected_ack, uint32_t& est_timeout) {
  mesh::Packet* pkt = composeMsgPacket(recipient, timestamp, attempt, text, expected_ack);
  if (pkt == NULL) return MSG_SEND_FAILED;

  uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());

  int rc;
  if (recipient.out_path_len < 0) {
    sendFloodScoped(recipient, pkt);
    txt_send_timeout = futureMillis(est_timeout = calcFloodTimeoutMillisFor(t));
    rc = MSG_SEND_SENT_FLOOD;
  } else {
    sendDirect(pkt, recipient.out_path, recipient.out_path_len);
    txt_send_timeout = futureMillis(est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len));
    rc = MSG_SEND_SENT_DIRECT;
  }
  return rc;
}

int  BaseChatMesh::sendCommandData(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text, uint32_t& est_timeout) {
  int text_len = strlen(text);
  if (text_len > MAX_TEXT_LEN) return MSG_SEND_FAILED;

  uint8_t temp[5+MAX_TEXT_LEN+1];
  memcpy(temp, &timestamp, 4);   // mostly an extra blob to help make packet_hash unique
  temp[4] = (attempt & 3) | (TXT_TYPE_CLI_DATA << 2);
  memcpy(&temp[5], text, text_len + 1);

  auto pkt = createDatagram(PAYLOAD_TYPE_TXT_MSG, recipient.id, recipient.getSharedSecret(self_id), temp, 5 + text_len);
  if (pkt == NULL) return MSG_SEND_FAILED;

  uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
  int rc;
  if (recipient.out_path_len < 0) {
    sendFloodScoped(recipient, pkt);
    txt_send_timeout = futureMillis(est_timeout = calcFloodTimeoutMillisFor(t));
    rc = MSG_SEND_SENT_FLOOD;
  } else {
    sendDirect(pkt, recipient.out_path, recipient.out_path_len);
    txt_send_timeout = futureMillis(est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len));
    rc = MSG_SEND_SENT_DIRECT;
  }
  return rc;
}

bool BaseChatMesh::sendGroupMessage(uint32_t timestamp, mesh::GroupChannel& channel, const char* sender_name, const char* text, int text_len) {
  uint8_t temp[5+MAX_TEXT_LEN+32];
  memcpy(temp, &timestamp, 4);   // mostly an extra blob to help make packet_hash unique
  temp[4] = 0;  // TXT_TYPE_PLAIN

  sprintf((char *) &temp[5], "%s: ", sender_name);  // <sender>: <msg>
  char *ep = strchr((char *) &temp[5], 0);
  int prefix_len = ep - (char *) &temp[5];

  if (text_len + prefix_len > MAX_TEXT_LEN) text_len = MAX_TEXT_LEN - prefix_len;
  memcpy(ep, text, text_len);
  ep[text_len] = 0;  // null terminator

  auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, channel, temp, 5 + prefix_len + text_len);
  if (pkt) {
    sendFloodScoped(channel, pkt);
    return true;
  }
  return false;
}

bool BaseChatMesh::shareContactZeroHop(const ContactInfo& contact) {
  int plen = getBlobByKey(contact.id.pub_key, PUB_KEY_SIZE, temp_buf);  // retrieve last raw advert packet
  if (plen == 0) return false;  // not found

  auto packet = obtainNewPacket();
  if (packet == NULL) return false;  // no Packets available

  packet->readFrom(temp_buf, plen);  // restore Packet from 'blob'
  uint16_t codes[2];
  codes[0] = codes[1] = 0;   // { 0, 0 } means 'send this nowhere'
  sendZeroHop(packet, codes);
  return true;  // success
}

uint8_t BaseChatMesh::exportContact(const ContactInfo& contact, uint8_t dest_buf[]) {
  return getBlobByKey(contact.id.pub_key, PUB_KEY_SIZE, dest_buf);  // retrieve last raw advert packet
}

bool BaseChatMesh::importContact(const uint8_t src_buf[], uint8_t len) {
  auto pkt = obtainNewPacket();
  if (pkt) {
    if (pkt->readFrom(src_buf, len) && pkt->getPayloadType() == PAYLOAD_TYPE_ADVERT) {
      pkt->header |= ROUTE_TYPE_FLOOD;   // simulate it being received flood-mode
      getTables()->clear(pkt);  // remove packet hash from table, so we can receive/process it again
      _pendingLoopback = pkt;  // loop-back, as if received over radio
      return true;  // success
    } else {
      releasePacket(pkt);   // undo the obtainNewPacket()
    }
  }
  return false; // error
}

int BaseChatMesh::sendLogin(const ContactInfo& recipient, const char* password, uint32_t& est_timeout) {
  mesh::Packet* pkt;
  {
    int tlen;
    uint8_t temp[24];
    uint32_t now = getRTCClock()->getCurrentTimeUnique();
    memcpy(temp, &now, 4);   // mostly an extra blob to help make packet_hash unique
    if (recipient.type == ADV_TYPE_ROOM) {
      memcpy(&temp[4], &recipient.sync_since, 4);
      int len = strlen(password); if (len > 15) len = 15;  // max 15 chars currently
      memcpy(&temp[8], password, len);
      tlen = 8 + len;
    } else {
      int len = strlen(password); if (len > 15) len = 15;  // max 15 chars currently
      memcpy(&temp[4], password, len);
      tlen = 4 + len;
    }

    pkt = createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, recipient.id, recipient.getSharedSecret(self_id), temp, tlen);
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len < 0) {
      sendFloodScoped(recipient, pkt);
      est_timeout = calcFloodTimeoutMillisFor(t);
      return MSG_SEND_SENT_FLOOD;
    } else {
      sendDirect(pkt, recipient.out_path, recipient.out_path_len);
      est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len);
      return MSG_SEND_SENT_DIRECT;
    }
  }
  return MSG_SEND_FAILED;
}

int BaseChatMesh::sendAnonReq(const ContactInfo& recipient, const uint8_t* data, uint8_t len, uint32_t& tag, uint32_t& est_timeout) {
  mesh::Packet* pkt;
  {
    uint8_t temp[MAX_PACKET_PAYLOAD];
    tag = getRTCClock()->getCurrentTimeUnique();
    memcpy(temp, &tag, 4);   // tag to match later (also extra blob to help make packet_hash unique)
    memcpy(&temp[4], data, len);

    pkt = createAnonDatagram(PAYLOAD_TYPE_ANON_REQ, self_id, recipient.id, recipient.getSharedSecret(self_id), temp, 4 + len);
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len < 0) {
      sendFloodScoped(recipient, pkt);
      est_timeout = calcFloodTimeoutMillisFor(t);
      return MSG_SEND_SENT_FLOOD;
    } else {
      sendDirect(pkt, recipient.out_path, recipient.out_path_len);
      est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len);
      return MSG_SEND_SENT_DIRECT;
    }
  }
  return MSG_SEND_FAILED;
}

int  BaseChatMesh::sendRequest(const ContactInfo& recipient, const uint8_t* req_data, uint8_t data_len, uint32_t& tag, uint32_t& est_timeout) {
  if (data_len > MAX_PACKET_PAYLOAD - 16) return MSG_SEND_FAILED;

  mesh::Packet* pkt;
  {
    uint8_t temp[MAX_PACKET_PAYLOAD];
    tag = getRTCClock()->getCurrentTimeUnique();
    memcpy(temp, &tag, 4);   // mostly an extra blob to help make packet_hash unique
    memcpy(&temp[4], req_data, data_len);

    pkt = createDatagram(PAYLOAD_TYPE_REQ, recipient.id, recipient.getSharedSecret(self_id), temp, 4 + data_len);
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len < 0) {
      sendFloodScoped(recipient, pkt);
      est_timeout = calcFloodTimeoutMillisFor(t);
      return MSG_SEND_SENT_FLOOD;
    } else {
      sendDirect(pkt, recipient.out_path, recipient.out_path_len);
      est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len);
      return MSG_SEND_SENT_DIRECT;
    }
  }
  return MSG_SEND_FAILED;
}

int  BaseChatMesh::sendRequest(const ContactInfo& recipient, uint8_t req_type, uint32_t& tag, uint32_t& est_timeout) {
  mesh::Packet* pkt;
  {
    uint8_t temp[13];
    tag = getRTCClock()->getCurrentTimeUnique();
    memcpy(temp, &tag, 4);   // mostly an extra blob to help make packet_hash unique
    temp[4] = req_type;
    memset(&temp[5], 0, 4);  // reserved (possibly for 'since' param)
    getRNG()->random(&temp[9], 4);   // random blob to help make packet-hash unique

    pkt = createDatagram(PAYLOAD_TYPE_REQ, recipient.id, recipient.getSharedSecret(self_id), temp, sizeof(temp));
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len < 0) {
      sendFloodScoped(recipient, pkt);
      est_timeout = calcFloodTimeoutMillisFor(t);
      return MSG_SEND_SENT_FLOOD;
    } else {
      sendDirect(pkt, recipient.out_path, recipient.out_path_len);
      est_timeout = calcDirectTimeoutMillisFor(t, recipient.out_path_len);
      return MSG_SEND_SENT_DIRECT;
    }
  }
  return MSG_SEND_FAILED;
}

bool BaseChatMesh::startConnection(const ContactInfo& contact, uint16_t keep_alive_secs) {
  int use_idx = -1;
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis == 0) {  // free slot?
      use_idx = i;
    } else if (connections[i].server_id.matches(contact.id)) {  // already in table?
      use_idx = i;
      break;
    }
  }
  if (use_idx < 0) {
    return false;   // table is full
  }
  connections[use_idx].server_id = contact.id;
  uint32_t interval = connections[use_idx].keep_alive_millis = ((uint32_t)keep_alive_secs)*1000;
  connections[use_idx].next_ping = futureMillis(interval);
  connections[use_idx].expected_ack = 0;
  connections[use_idx].last_activity = getRTCClock()->getCurrentTime();
  return true;  // success
}

void BaseChatMesh::stopConnection(const uint8_t* pub_key) {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].server_id.matches(pub_key)) {
      connections[i].keep_alive_millis = 0;  // mark slot as now free
      connections[i].next_ping = 0;
      connections[i].expected_ack = 0;
      connections[i].last_activity = 0;
      break;
    }
  }
}

bool BaseChatMesh::hasConnectionTo(const uint8_t* pub_key) {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis > 0 && connections[i].server_id.matches(pub_key)) return true;
  }
  return false;
}

void BaseChatMesh::markConnectionActive(const ContactInfo& contact) {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis > 0 && connections[i].server_id.matches(contact.id)) {
      connections[i].last_activity = getRTCClock()->getCurrentTime();

      // re-schedule next KEEP_ALIVE, now that we have heard from server
      connections[i].next_ping = futureMillis(connections[i].keep_alive_millis);
      break;
    }
  }
}

ContactInfo* BaseChatMesh::checkConnectionsAck(const uint8_t* data) {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis > 0 && memcmp(&connections[i].expected_ack, data, 4) == 0) {
      // yes, got an ack for our keep_alive request!
      connections[i].expected_ack = 0;
      connections[i].last_activity = getRTCClock()->getCurrentTime();

      // re-schedule next KEEP_ALIVE, now that we have heard from server
      connections[i].next_ping = futureMillis(connections[i].keep_alive_millis);

      auto id = &connections[i].server_id;
      return lookupContactByPubKey(id->pub_key, PUB_KEY_SIZE);  // yes, a match
    }
  }
  return NULL;  /// no match
}

void BaseChatMesh::checkConnections() {
  // scan connections[] table, send KEEP_ALIVE requests
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (connections[i].keep_alive_millis == 0) continue;  // unused slot

    uint32_t now = getRTCClock()->getCurrentTime();
    uint32_t expire_secs = (connections[i].keep_alive_millis / 1000) * 5 / 2;   // 2.5 x keep_alive interval
    if (now >= connections[i].last_activity + expire_secs) {
      // connection now lost
      connections[i].keep_alive_millis = 0;
      connections[i].next_ping = 0;
      connections[i].expected_ack = 0;
      connections[i].last_activity = 0;
      continue;
    }

    if (millisHasNowPassed(connections[i].next_ping)) {
      auto contact = lookupContactByPubKey(connections[i].server_id.pub_key, PUB_KEY_SIZE);
      if (contact == NULL) {
        MESH_DEBUG_PRINTLN("checkConnections(): Keep_alive contact not found!");
        continue;
      }
      if (contact->out_path_len < 0) {
        MESH_DEBUG_PRINTLN("checkConnections(): Keep_alive contact, no out_path!");
        continue;
      }

      // send KEEP_ALIVE request
      uint8_t data[9];
      uint32_t now = getRTCClock()->getCurrentTimeUnique();
      memcpy(data, &now, 4);
      data[4] = REQ_TYPE_KEEP_ALIVE;
      memcpy(&data[5], &contact->sync_since, 4);
    
      // calc expected ACK reply
      mesh::Utils::sha256((uint8_t *)&connections[i].expected_ack, 4, data, 9, self_id.pub_key, PUB_KEY_SIZE);

      auto pkt = createDatagram(PAYLOAD_TYPE_REQ, contact->id, contact->getSharedSecret(self_id), data, 9);
      if (pkt) {
        sendDirect(pkt, contact->out_path, contact->out_path_len);
      }
    
      // schedule next KEEP_ALIVE
      connections[i].next_ping = futureMillis(connections[i].keep_alive_millis);
    }
  }
}

void BaseChatMesh::resetPathTo(ContactInfo& recipient) {
  recipient.out_path_len = -1;
}

static ContactInfo* table;  // pass via global :-(

static int cmp_adv_timestamp(const void *a, const void *b) {
  int a_idx = *((int *)a);
  int b_idx = *((int *)b);
  if (table[b_idx].last_advert_timestamp > table[a_idx].last_advert_timestamp) return 1;
  if (table[b_idx].last_advert_timestamp < table[a_idx].last_advert_timestamp) return -1;
  return 0;
}

void BaseChatMesh::scanRecentContacts(int last_n, ContactVisitor* visitor) {
  for (int i = 0; i < num_contacts; i++) {  // sort the INDEXES into contacts[]
    sort_array[i] = i;
  }
  table = contacts; // pass via global *sigh* :-(
  qsort(sort_array, num_contacts, sizeof(sort_array[0]), cmp_adv_timestamp);

  if (last_n == 0) {
    last_n = num_contacts;   // scan ALL
  } else {
    if (last_n > num_contacts) last_n = num_contacts;
  }
  for (int i = 0; i < last_n; i++) {
    visitor->onContactVisit(contacts[sort_array[i]]);
  }
}

ContactInfo* BaseChatMesh::searchContactsByPrefix(const char* name_prefix) {
  int len = strlen(name_prefix);
  for (int i = 0; i < num_contacts; i++) {
    auto c = &contacts[i];
    if (memcmp(c->name, name_prefix, len) == 0) return c;
  }
  return NULL;  // not found
}

ContactInfo* BaseChatMesh::lookupContactByPubKey(const uint8_t* pub_key, int prefix_len) {
  for (int i = 0; i < num_contacts; i++) {
    auto c = &contacts[i];
    if (memcmp(c->id.pub_key, pub_key, prefix_len) == 0) return c;
  }
  return NULL;  // not found
}

bool BaseChatMesh::addContact(const ContactInfo& contact) {
  ContactInfo* dest = allocateContactSlot();
  if (dest) {
    *dest = contact;
    dest->shared_secret_valid = false; // mark shared_secret as needing calculation
    return true;  // success
  }
  return false;
}

bool BaseChatMesh::removeContact(ContactInfo& contact) {
  int idx = 0;
  while (idx < num_contacts && !contacts[idx].id.matches(contact.id)) {
    idx++;
  }
  if (idx >= num_contacts) return false;   // not found

  // remove from contacts array
  num_contacts--;
  while (idx < num_contacts) {
    contacts[idx] = contacts[idx + 1];
    idx++;
  }
  return true;  // Success
}

#ifdef MAX_GROUP_CHANNELS
#include <base64.hpp>

ChannelDetails* BaseChatMesh::addChannel(const char* name, const char* psk_base64) {
  if (num_channels < MAX_GROUP_CHANNELS) {
    auto dest = &channels[num_channels];

    memset(dest->channel.secret, 0, sizeof(dest->channel.secret));
    int len = decode_base64((unsigned char *) psk_base64, strlen(psk_base64), dest->channel.secret);
    if (len == 32 || len == 16) {
      mesh::Utils::sha256(dest->channel.hash, sizeof(dest->channel.hash), dest->channel.secret, len);
      StrHelper::strncpy(dest->name, name, sizeof(dest->name));
      num_channels++;
      return dest;
    }
  }
  return NULL;
}
bool BaseChatMesh::getChannel(int idx, ChannelDetails& dest) {
  if (idx >= 0 && idx < MAX_GROUP_CHANNELS) {
    dest = channels[idx];
    return true;
  }
  return false;
}
bool BaseChatMesh::setChannel(int idx, const ChannelDetails& src) {
  static uint8_t zeroes[] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };

  if (idx >= 0 && idx < MAX_GROUP_CHANNELS) {
    channels[idx] = src;
    if (memcmp(&src.channel.secret[16], zeroes, 16) == 0) {
      mesh::Utils::sha256(channels[idx].channel.hash, sizeof(channels[idx].channel.hash), src.channel.secret, 16);  // 128-bit key
    } else {
      mesh::Utils::sha256(channels[idx].channel.hash, sizeof(channels[idx].channel.hash), src.channel.secret, 32);  // 256-bit key
    }
    return true;
  }
  return false;
}
int BaseChatMesh::findChannelIdx(const mesh::GroupChannel& ch) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (memcmp(ch.secret, channels[i].channel.secret, sizeof(ch.secret)) == 0) return i;
  }
  return -1;  // not found
}
#else
ChannelDetails* BaseChatMesh::addChannel(const char* name, const char* psk_base64) {
  return NULL;  // not supported
}
bool BaseChatMesh::getChannel(int idx, ChannelDetails& dest) {
  return false;
}
bool BaseChatMesh::setChannel(int idx, const ChannelDetails& src) {
  return false;
}
int BaseChatMesh::findChannelIdx(const mesh::GroupChannel& ch) {
  return -1;  // not found
}
#endif

bool BaseChatMesh::getContactByIdx(uint32_t idx, ContactInfo& contact) {
  if (idx >= num_contacts) return false;

  contact = contacts[idx];
  return true;
}

ContactsIterator BaseChatMesh::startContactsIterator() {
  return ContactsIterator();
}

bool ContactsIterator::hasNext(const BaseChatMesh* mesh, ContactInfo& dest) {
  if (next_idx >= mesh->getNumContacts()) return false;

  dest = mesh->contacts[next_idx++];
  return true;
}

void BaseChatMesh::loop() {
  Mesh::loop();

  if (txt_send_timeout && millisHasNowPassed(txt_send_timeout)) {
    // failed to get an ACK
    onSendTimeout();
    txt_send_timeout = 0;
  }

  if (_pendingLoopback) {
    onRecvPacket(_pendingLoopback);  // loop-back, as if received over radio
    releasePacket(_pendingLoopback);   // undo the obtainNewPacket()
    _pendingLoopback = NULL;
  }
}
