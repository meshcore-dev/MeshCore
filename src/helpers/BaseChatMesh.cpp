#include <helpers/BaseChatMesh.h>
#include <Utils.h>
#include <SHA256.h>
#include <ed_25519.h>

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY   300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY     200
#endif

uint16_t BaseChatMesh::nextAeadNonceFor(const ContactInfo& contact) {
  uint16_t nonce = contact.nextAeadNonce();
  if (nonce != 0) {
    int idx = &contact - contacts;
    if (idx >= 0 && idx < num_contacts &&
        (uint16_t)(contact.aead_nonce - nonce_at_last_persist[idx]) >= NONCE_PERSIST_INTERVAL) {
      nonce_dirty = true;
    }
  }
  return nonce;
}

bool BaseChatMesh::applyLoadedNonce(const uint8_t* pub_key_prefix, uint16_t nonce) {
  for (int i = 0; i < num_contacts; i++) {
    if (memcmp(contacts[i].id.pub_key, pub_key_prefix, 4) == 0) {
      contacts[i].aead_nonce = nonce;
      return true;
    }
  }
  return false;
}

void BaseChatMesh::finalizeNonceLoad(bool needs_bump) {
  for (int i = 0; i < num_contacts; i++) {
    if (needs_bump) {
      uint16_t old = contacts[i].aead_nonce;
      contacts[i].aead_nonce += NONCE_BOOT_BUMP;
      if (contacts[i].aead_nonce == 0) contacts[i].aead_nonce = 1;
      if (contacts[i].aead_nonce < old) {
        MESH_DEBUG_PRINTLN("AEAD nonce wrapped after boot bump for peer: %s", contacts[i].name);
      }
    }
    nonce_at_last_persist[i] = contacts[i].aead_nonce;
  }
  nonce_dirty = false;

  // Apply boot bump to session key nonces too
  if (needs_bump) {
    for (int i = 0; i < session_keys.getCount(); i++) {
      auto entry = session_keys.getByIdx(i);
      if (entry && (entry->state == SESSION_STATE_ACTIVE || entry->state == SESSION_STATE_DUAL_DECODE)) {
        uint16_t old_nonce = entry->nonce;
        entry->nonce += NONCE_BOOT_BUMP;
        if (entry->nonce <= old_nonce) {
          entry->nonce = 65535;  // wrapped — force exhaustion so renegotiation happens
        }
      }
    }
  }
}

bool BaseChatMesh::getNonceEntry(int idx, uint8_t* pub_key_prefix, uint16_t* nonce) {
  if (idx >= num_contacts) return false;
  memcpy(pub_key_prefix, contacts[idx].id.pub_key, 4);
  *nonce = contacts[idx].aead_nonce;
  return true;
}

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
    builder.setFeat1(FEAT1_AEAD_SUPPORT);
    app_data_len = builder.encodeTo(app_data);
  }

  return createAdvert(self_id, app_data, app_data_len);
}

mesh::Packet* BaseChatMesh::createSelfAdvert(const char* name, double lat, double lon) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len;
  {
    AdvertDataBuilder builder(ADV_TYPE_CHAT, name, lat, lon);
    builder.setFeat1(FEAT1_AEAD_SUPPORT);
    app_data_len = builder.encodeTo(app_data);
  }

  return createAdvert(self_id, app_data, app_data_len);
}

void BaseChatMesh::sendAckTo(const ContactInfo& dest, uint32_t ack_hash) {
  if (dest.out_path_len == OUT_PATH_UNKNOWN) {
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
  ci.out_path_len = OUT_PATH_UNKNOWN;
  StrHelper::strncpy(ci.name, parser.getName(), sizeof(ci.name));
  ci.type = parser.getType();
  if (parser.hasLatLon()) {
    ci.gps_lat = parser.getIntLat();
    ci.gps_lon = parser.getIntLon();
  }
  ci.last_advert_timestamp = timestamp;
  ci.lastmod = getRTCClock()->getCurrentTime();
  ci.aead_nonce = (uint16_t)getRNG()->nextInt(NONCE_INITIAL_MIN, NONCE_INITIAL_MAX + 1);
  if (parser.getFeat1() & FEAT1_AEAD_SUPPORT) {
    ci.flags |= CONTACT_FLAG_AEAD;
  }
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
    
    populateContactFromAdvert(*from, id, parser, timestamp);  // seeds aead_nonce from RNG
    nonce_at_last_persist[from - contacts] = from->aead_nonce;
    from->sync_since = 0;
    from->shared_secret_valid = false;
  }
  // update
    putBlobByKey(id.pub_key, PUB_KEY_SIZE, temp_buf, plen);
    StrHelper::strncpy(from->name, parser.getName(), sizeof(from->name));
    from->type = parser.getType();
    if (parser.hasLatLon()) {
      from->gps_lat = parser.getIntLat();
      from->gps_lon = parser.getIntLon();
    }
    from->last_advert_timestamp = timestamp;
    from->lastmod = getRTCClock()->getCurrentTime();
    if (parser.getFeat1() & FEAT1_AEAD_SUPPORT) {
      from->flags |= CONTACT_FLAG_AEAD;
    } else {
      from->flags &= ~CONTACT_FLAG_AEAD;
    }

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

uint8_t BaseChatMesh::getPeerFlags(int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < num_contacts) {
    return contacts[i].flags;
  }
  return 0;
}

uint16_t BaseChatMesh::getPeerNextAeadNonce(int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < num_contacts) {
    return nextAeadNonceFor(contacts[i]);
  }
  return 0;
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
        mesh::Packet* path = createPathReturn(from.id, getEncryptionKeyFor(from), packet->path, packet->path_len,
                                                PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 4, getEncryptionNonceFor(from));
        if (path) sendFloodScoped(from, path, TXT_ACK_DELAY);
      } else {
        sendAckTo(from, ack_hash);
      }
    } else if (flags == TXT_TYPE_CLI_DATA) {
      onCommandDataRecv(from, packet, timestamp, (const char *) &data[5]);  // let UI know
      // NOTE: no ack expected for CLI_DATA replies

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect() (NOTE: no ACK as extra)
        mesh::Packet* path = createPathReturn(from.id, getEncryptionKeyFor(from), packet->path, packet->path_len, 0, NULL, 0, getEncryptionNonceFor(from));
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
        mesh::Packet* path = createPathReturn(from.id, getEncryptionKeyFor(from), packet->path, packet->path_len,
                                                PAYLOAD_TYPE_ACK, (uint8_t *) &ack_hash, 4, getEncryptionNonceFor(from));
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

    uint8_t reply_len = 0;
    bool use_static_secret = false;

    // Intercept session key INIT before subclass onContactRequest
    if (len >= 5 + PUB_KEY_SIZE && data[4] == REQ_TYPE_SESSION_KEY_INIT) {
      memcpy(temp_buf, &sender_timestamp, 4);
      temp_buf[4] = RESP_TYPE_SESSION_KEY_ACCEPT;
      uint8_t n = handleIncomingSessionKeyInit(from, &data[5], &temp_buf[5]);
      if (n > 0) {
        reply_len = 5 + n;
        use_static_secret = true;  // ACCEPT must use static secret (initiator doesn't have session key yet)
      }
    }
    if (reply_len == 0) {
      reply_len = onContactRequest(from, sender_timestamp, &data[4], len - 4, temp_buf);
    }

    if (reply_len > 0) {
      // Session key ACCEPT must be encrypted with static ECDH secret, because
      // the initiator hasn't derived the session key yet (they need our ephemeral_pub_B first).
      const uint8_t* enc_key = use_static_secret ? from.getSharedSecret(self_id) : getEncryptionKeyFor(from);
      uint16_t enc_nonce = use_static_secret ? nextAeadNonceFor(from) : getEncryptionNonceFor(from);

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
        mesh::Packet* path = createPathReturn(from.id, enc_key, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, temp_buf, reply_len, enc_nonce);
        if (path) sendFloodScoped(from, path, SERVER_RESPONSE_DELAY);
      } else {
        mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, from.id, enc_key, temp_buf, reply_len, enc_nonce);
        if (reply) {
          if (from.out_path_len != OUT_PATH_UNKNOWN) {  // we have an out_path, so send DIRECT
            sendDirect(reply, from.out_path, from.out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFloodScoped(from, reply, SERVER_RESPONSE_DELAY);
          }
        }
      }
    }
  } else if (type == PAYLOAD_TYPE_RESPONSE && len > 0) {
    // Intercept session key accept responses before passing to onContactResponse.
    // Note: RESP_TYPE_SESSION_KEY_ACCEPT (0x08) could collide with a normal response whose
    // 5th byte happens to be 0x08, but handleSessionKeyResponse has a secondary guard
    // (requires INIT_SENT state for this peer) so false positives are extremely unlikely,
    // and self-heal via session key invalidation if they ever occur.
    if (len >= 5 && data[4] == RESP_TYPE_SESSION_KEY_ACCEPT && handleSessionKeyResponse(from, data, len)) {
      // Session key response handled — don't pass to onContactResponse
    } else {
      onContactResponse(from, data, len);
    }
    if (packet->isRouteFlood() && from.out_path_len != OUT_PATH_UNKNOWN) {
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
  from.out_path_len = mesh::Packet::copyPath(from.out_path, out_path, out_path_len);  // store a copy of path, for sendDirect()
  from.lastmod = getRTCClock()->getCurrentTime();

  onContactPathUpdated(from);

  if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4) {
    // also got an encoded ACK!
    if (processAck(extra) != NULL) {
      txt_send_timeout = 0;   // matched one we're waiting for, cancel timeout timer
    }
  } else if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 0) {
    if (extra_len >= 5 && extra[4] == RESP_TYPE_SESSION_KEY_ACCEPT && handleSessionKeyResponse(from, extra, extra_len)) {
      // Session key response handled
    } else {
      onContactResponse(from, extra, extra_len);
    }
  }
  return true;  // send reciprocal path if necessary
}

void BaseChatMesh::onAckRecv(mesh::Packet* packet, uint32_t ack_crc) {
  ContactInfo* from;
  if ((from = processAck((uint8_t *)&ack_crc)) != NULL) {
    txt_send_timeout = 0;   // matched one we're waiting for, cancel timeout timer
    packet->markDoNotRetransmit();   // ACK was for this node, so don't retransmit

    if (packet->isRouteFlood() && from->out_path_len != OUT_PATH_UNKNOWN) {
      // we have direct path, but other node is still sending flood, so maybe they didn't receive reciprocal path properly(?)
      handleReturnPathRetry(*from, packet->path, packet->path_len);
    }
  }
}

void BaseChatMesh::handleReturnPathRetry(const ContactInfo& contact, const uint8_t* path, uint8_t path_len) {
  // NOTE: simplest impl is just to re-send a reciprocal return path to sender (DIRECTLY)
  //        override this method in various firmwares, if there's a better strategy
  mesh::Packet* rpath = createPathReturn(contact.id, getEncryptionKeyFor(contact), path, path_len, 0, NULL, 0, getEncryptionNonceFor(contact));
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

  return createDatagram(PAYLOAD_TYPE_TXT_MSG, recipient.id, getEncryptionKeyFor(recipient), temp, len, getEncryptionNonceFor(recipient));
}

int  BaseChatMesh::sendMessage(const ContactInfo& recipient, uint32_t timestamp, uint8_t attempt, const char* text, uint32_t& expected_ack, uint32_t& est_timeout) {
  mesh::Packet* pkt = composeMsgPacket(recipient, timestamp, attempt, text, expected_ack);
  if (pkt == NULL) return MSG_SEND_FAILED;

  uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());

  int rc;
  if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
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

  auto pkt = createDatagram(PAYLOAD_TYPE_TXT_MSG, recipient.id, getEncryptionKeyFor(recipient), temp, 5 + text_len, getEncryptionNonceFor(recipient));
  if (pkt == NULL) return MSG_SEND_FAILED;

  uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
  int rc;
  if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
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
    if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
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
    if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
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

    pkt = createDatagram(PAYLOAD_TYPE_REQ, recipient.id, getEncryptionKeyFor(recipient), temp, 4 + data_len, getEncryptionNonceFor(recipient));
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
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

    pkt = createDatagram(PAYLOAD_TYPE_REQ, recipient.id, getEncryptionKeyFor(recipient), temp, sizeof(temp), getEncryptionNonceFor(recipient));
  }
  if (pkt) {
    uint32_t t = _radio->getEstAirtimeFor(pkt->getRawLength());
    if (recipient.out_path_len == OUT_PATH_UNKNOWN) {
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
      if (contact->out_path_len == OUT_PATH_UNKNOWN) {
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

      auto pkt = createDatagram(PAYLOAD_TYPE_REQ, contact->id, getEncryptionKeyFor(*contact), data, 9, getEncryptionNonceFor(*contact));
      if (pkt) {
        sendDirect(pkt, contact->out_path, contact->out_path_len);
      }
    
      // schedule next KEEP_ALIVE
      connections[i].next_ping = futureMillis(connections[i].keep_alive_millis);
    }
  }
}

void BaseChatMesh::resetPathTo(ContactInfo& recipient) {
  recipient.out_path_len = OUT_PATH_UNKNOWN;
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
    int idx = dest - contacts;
    *dest = contact;
    dest->shared_secret_valid = false; // mark shared_secret as needing calculation
    dest->aead_nonce = (uint16_t)getRNG()->nextInt(NONCE_INITIAL_MIN, NONCE_INITIAL_MAX + 1);
    nonce_at_last_persist[idx] = dest->aead_nonce;
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

  session_keys.remove(contact.id.pub_key);  // also remove session key if any

  // remove from contacts array and parallel nonce tracking
  num_contacts--;
  while (idx < num_contacts) {
    contacts[idx] = contacts[idx + 1];
    nonce_at_last_persist[idx] = nonce_at_last_persist[idx + 1];
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

  checkSessionKeyTimeouts();

  // Process deferred session key negotiation (set by getEncryptionNonceFor)
  if (_pending_rekey_idx >= 0 && _pending_rekey_idx < num_contacts) {
    int idx = _pending_rekey_idx;
    _pending_rekey_idx = -1;
    initiateSessionKeyNegotiation(contacts[idx]);
  }
}

// --- Session key support (Phase 2 — initiator) ---

static bool canUseSessionKey(const SessionKeyEntry* entry) {
  if (!entry) return false;
  // ACTIVE/DUAL_DECODE: normal session key use
  // INIT_SENT with nonce > 1: renegotiation in progress, keep using old session key
  //   (nonce == 0 means fresh allocation with no prior session key)
  bool valid_state = (entry->state == SESSION_STATE_ACTIVE || entry->state == SESSION_STATE_DUAL_DECODE)
                     || (entry->state == SESSION_STATE_INIT_SENT && entry->nonce > 1);
  return valid_state
      && entry->sends_since_last_recv < SESSION_KEY_STALE_THRESHOLD
      && entry->nonce < 65535;  // nonce exhausted → fall back to static ECDH
}

const uint8_t* BaseChatMesh::getEncryptionKeyFor(const ContactInfo& contact) {
  auto entry = session_keys.findByPrefix(contact.id.pub_key);
  if (canUseSessionKey(entry)) {
    return entry->session_key;
  }
  return contact.getSharedSecret(self_id);
}

uint16_t BaseChatMesh::getEncryptionNonceFor(const ContactInfo& contact) {
  uint16_t nonce = 0;
  auto entry = session_keys.findByPrefix(contact.id.pub_key);
  if (canUseSessionKey(entry)) {
    ++entry->nonce;
    if (entry->sends_since_last_recv < 255) entry->sends_since_last_recv++;
    session_keys_dirty = true;
    nonce = entry->nonce;
  } else if (entry && entry->sends_since_last_recv < 255) {
    // Progressive fallback: keep incrementing counter even when not using session key
    entry->sends_since_last_recv++;
    if (entry->sends_since_last_recv >= SESSION_KEY_ABANDON_THRESHOLD) {
      // Give up: clear AEAD capability and remove session key
      int idx = &contact - contacts;
      if (idx >= 0 && idx < num_contacts)
        contacts[idx].flags &= ~CONTACT_FLAG_AEAD;
      session_keys.remove(contact.id.pub_key);
      onSessionKeysUpdated();
      // nonce = 0 (ECB)
    } else if (entry->sends_since_last_recv >= SESSION_KEY_ECB_THRESHOLD) {
      // nonce = 0 (ECB)
    } else {
      nonce = nextAeadNonceFor(contact);
    }
  } else {
    nonce = nextAeadNonceFor(contact);
  }

  // Trigger session key negotiation on the next loop() tick.
  // Checking here (the single funnel for all outgoing encryption) ensures no
  // send path can silently skip a trigger — unlike the old per-call-site approach.
  if (_pending_rekey_idx < 0 && shouldInitiateSessionKey(contact)) {
    _pending_rekey_idx = &contact - contacts;
  }

  return nonce;
}

bool BaseChatMesh::shouldInitiateSessionKey(const ContactInfo& contact) {
  // Only for AEAD-capable peers
  if (!(contact.flags & CONTACT_FLAG_AEAD)) return false;

  // Need a known path to send the request
  if (contact.out_path_len < 0) return false;

  auto entry = session_keys.findByPrefix(contact.id.pub_key);

  // Don't trigger if negotiation already in progress
  if (entry && entry->state == SESSION_STATE_INIT_SENT) return false;

  // Determine intervals based on hop count tier:
  //   direct (0):  static=100, session=100
  //   1–9 hops:    static=500, session=300
  //   10+ hops:    static=1000, session=300
  uint16_t static_interval, session_interval;
  if (contact.out_path_len == 0) {
    static_interval = 100;
    session_interval = 100;
  } else if (contact.out_path_len < 10) {
    static_interval = 500;
    session_interval = 300;
  } else {
    static_interval = 1000;
    session_interval = 300;
  }

  if (entry && (entry->state == SESSION_STATE_ACTIVE || entry->state == SESSION_STATE_DUAL_DECODE)) {
    if (entry->nonce < 65535) {
      // Active session key with remaining nonces — renegotiate after nonce > 60000
      if (entry->nonce <= NONCE_REKEY_THRESHOLD) return false;
      return ((entry->nonce - NONCE_REKEY_THRESHOLD) % session_interval) == 0;
    }
    // Session key nonce exhausted — fall through to static ECDH trigger
  }

  // No session key (or state=NONE) — trigger based on static ECDH nonce vs interval
  if (contact.aead_nonce == 0) return false;  // no messages sent yet
  return (contact.aead_nonce % static_interval) == 0;
}

bool BaseChatMesh::initiateSessionKeyNegotiation(const ContactInfo& contact) {
  auto entry = session_keys.allocate(contact.id.pub_key);
  if (!entry) return false;

  // Don't start a new negotiation if one is already pending
  if (entry->state == SESSION_STATE_INIT_SENT) return false;

  // Generate ephemeral keypair A
  uint8_t seed[SEED_SIZE];
  getRNG()->random(seed, SEED_SIZE);
  ed25519_create_keypair(entry->ephemeral_pub, entry->ephemeral_prv, seed);
  memset(seed, 0, SEED_SIZE);

  // Send REQ_TYPE_SESSION_KEY_INIT with ephemeral_pub_A
  uint8_t req_data[1 + PUB_KEY_SIZE];
  req_data[0] = REQ_TYPE_SESSION_KEY_INIT;
  memcpy(&req_data[1], entry->ephemeral_pub, PUB_KEY_SIZE);

  uint32_t tag, est_timeout;
  int rc = sendRequest(contact, req_data, sizeof(req_data), tag, est_timeout);
  if (rc == MSG_SEND_FAILED) {
    memset(entry->ephemeral_prv, 0, PRV_KEY_SIZE);
    memset(entry->ephemeral_pub, 0, PUB_KEY_SIZE);
    return false;
  }

  entry->state = SESSION_STATE_INIT_SENT;
  entry->retries_left = SESSION_KEY_MAX_RETRIES - 1;
  entry->timeout_at = futureMillis(SESSION_KEY_TIMEOUT_MS);
  return true;
}

bool BaseChatMesh::handleSessionKeyResponse(ContactInfo& contact, const uint8_t* data, uint8_t len) {
  // Response format: [timestamp:4][RESP_TYPE_SESSION_KEY_ACCEPT:1][ephemeral_pub_B:32]
  if (len < 5 + PUB_KEY_SIZE) return false;
  if (data[4] != RESP_TYPE_SESSION_KEY_ACCEPT) return false;

  auto entry = session_keys.findByPrefix(contact.id.pub_key);
  if (!entry || entry->state != SESSION_STATE_INIT_SENT) return false;

  const uint8_t* ephemeral_pub_B = &data[5];

  // Compute ephemeral_secret via X25519
  uint8_t ephemeral_secret[PUB_KEY_SIZE];
  ed25519_key_exchange(ephemeral_secret, ephemeral_pub_B, entry->ephemeral_prv);
  memset(entry->ephemeral_prv, 0, PRV_KEY_SIZE);
  memset(entry->ephemeral_pub, 0, PUB_KEY_SIZE);

  // Derive session_key = HMAC-SHA256(static_shared_secret, ephemeral_secret)
  const uint8_t* static_secret = contact.getSharedSecret(self_id);
  uint8_t new_session_key[SESSION_KEY_SIZE];
  {
    SHA256 sha;
    sha.resetHMAC(static_secret, PUB_KEY_SIZE);
    sha.update(ephemeral_secret, PUB_KEY_SIZE);
    sha.finalizeHMAC(static_secret, PUB_KEY_SIZE, new_session_key, SESSION_KEY_SIZE);
  }
  memset(ephemeral_secret, 0, PUB_KEY_SIZE);

  // Activate session key
  memcpy(entry->session_key, new_session_key, SESSION_KEY_SIZE);
  memset(new_session_key, 0, SESSION_KEY_SIZE);
  entry->nonce = 1;
  entry->state = SESSION_STATE_ACTIVE;
  entry->sends_since_last_recv = 0;
  entry->retries_left = 0;
  entry->timeout_at = 0;

  MESH_DEBUG_PRINTLN("Session key established with: %s", contact.name);
  onSessionKeysUpdated();
  return true;
}

uint8_t BaseChatMesh::handleIncomingSessionKeyInit(ContactInfo& from, const uint8_t* ephemeral_pub_A, uint8_t* reply_buf) {
  // 1. Generate ephemeral keypair B
  uint8_t seed[SEED_SIZE];
  getRNG()->random(seed, SEED_SIZE);
  uint8_t ephemeral_pub_B[PUB_KEY_SIZE];
  uint8_t ephemeral_prv_B[PRV_KEY_SIZE];
  ed25519_create_keypair(ephemeral_pub_B, ephemeral_prv_B, seed);
  memset(seed, 0, SEED_SIZE);

  // 2. Compute ephemeral_secret via X25519
  uint8_t ephemeral_secret[PUB_KEY_SIZE];
  ed25519_key_exchange(ephemeral_secret, ephemeral_pub_A, ephemeral_prv_B);
  memset(ephemeral_prv_B, 0, PRV_KEY_SIZE);

  // 3. Derive session_key = HMAC-SHA256(static_shared_secret, ephemeral_secret)
  const uint8_t* static_secret = from.getSharedSecret(self_id);
  uint8_t new_session_key[SESSION_KEY_SIZE];
  {
    SHA256 sha;
    sha.resetHMAC(static_secret, PUB_KEY_SIZE);
    sha.update(ephemeral_secret, PUB_KEY_SIZE);
    sha.finalizeHMAC(static_secret, PUB_KEY_SIZE, new_session_key, SESSION_KEY_SIZE);
  }
  memset(ephemeral_secret, 0, PUB_KEY_SIZE);

  // 4. Store in pool (dual-decode: new key active, old key still valid)
  auto entry = session_keys.allocate(from.id.pub_key);
  if (!entry) return 0;

  if (entry->state == SESSION_STATE_ACTIVE || entry->state == SESSION_STATE_DUAL_DECODE) {
    memcpy(entry->prev_session_key, entry->session_key, SESSION_KEY_SIZE);
  }
  memcpy(entry->session_key, new_session_key, SESSION_KEY_SIZE);
  entry->nonce = 1;
  entry->state = SESSION_STATE_DUAL_DECODE;
  entry->sends_since_last_recv = 0;
  memset(new_session_key, 0, SESSION_KEY_SIZE);

  // 5. Persist immediately
  onSessionKeysUpdated();

  // 6. Write ephemeral_pub_B to reply
  memcpy(reply_buf, ephemeral_pub_B, PUB_KEY_SIZE);
  MESH_DEBUG_PRINTLN("Session key INIT accepted from: %s", from.name);
  return PUB_KEY_SIZE;
}

void BaseChatMesh::checkSessionKeyTimeouts() {
  for (int i = 0; i < session_keys.getCount(); i++) {
    auto entry = session_keys.getByIdx(i);
    if (!entry || entry->state != SESSION_STATE_INIT_SENT) continue;
    if (entry->timeout_at == 0 || !millisHasNowPassed(entry->timeout_at)) continue;

    if (entry->retries_left > 0) {
      // Retry: find the contact and resend INIT
      for (int j = 0; j < num_contacts; j++) {
        if (memcmp(contacts[j].id.pub_key, entry->peer_pub_prefix, 4) == 0) {
          entry->retries_left--;
          entry->timeout_at = futureMillis(SESSION_KEY_TIMEOUT_MS);

          // Regenerate ephemeral keypair for retry
          uint8_t seed[SEED_SIZE];
          getRNG()->random(seed, SEED_SIZE);
          ed25519_create_keypair(entry->ephemeral_pub, entry->ephemeral_prv, seed);
          memset(seed, 0, SEED_SIZE);

          uint8_t req_data[1 + PUB_KEY_SIZE];
          req_data[0] = REQ_TYPE_SESSION_KEY_INIT;
          memcpy(&req_data[1], entry->ephemeral_pub, PUB_KEY_SIZE);

          uint32_t tag, est_timeout;
          sendRequest(contacts[j], req_data, sizeof(req_data), tag, est_timeout);
          break;
        }
      }
    } else {
      // All retries exhausted — clean up
      memset(entry->ephemeral_prv, 0, PRV_KEY_SIZE);
      memset(entry->ephemeral_pub, 0, PUB_KEY_SIZE);
      entry->state = SESSION_STATE_NONE;
      entry->timeout_at = 0;
    }
  }
}

// Virtual overrides for session key decrypt path
const uint8_t* BaseChatMesh::getPeerSessionKey(int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < num_contacts) {
    auto entry = session_keys.findByPrefix(contacts[i].id.pub_key);
    // Also try decode during INIT_SENT renegotiation (nonce > 1 means prior key exists)
    if (entry && (entry->state == SESSION_STATE_ACTIVE || entry->state == SESSION_STATE_DUAL_DECODE
                  || (entry->state == SESSION_STATE_INIT_SENT && entry->nonce > 1)))
      return entry->session_key;
  }
  return nullptr;
}

const uint8_t* BaseChatMesh::getPeerPrevSessionKey(int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < num_contacts) {
    auto entry = session_keys.findByPrefix(contacts[i].id.pub_key);
    if (entry && entry->state == SESSION_STATE_DUAL_DECODE)
      return entry->prev_session_key;
  }
  return nullptr;
}

void BaseChatMesh::onSessionKeyDecryptSuccess(int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < num_contacts) {
    auto entry = session_keys.findByPrefix(contacts[i].id.pub_key);
    if (entry) {
      bool changed = (entry->state == SESSION_STATE_DUAL_DECODE);
      if (changed) {
        memset(entry->prev_session_key, 0, SESSION_KEY_SIZE);
        entry->state = SESSION_STATE_ACTIVE;
      }
      entry->sends_since_last_recv = 0;
      if (changed) onSessionKeysUpdated();
    }
  }
}

const uint8_t* BaseChatMesh::getPeerEncryptionKey(int peer_idx, const uint8_t* static_secret) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < num_contacts)
    return getEncryptionKeyFor(contacts[i]);
  return static_secret;
}

uint16_t BaseChatMesh::getPeerEncryptionNonce(int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < num_contacts)
    return getEncryptionNonceFor(contacts[i]);
  return getPeerNextAeadNonce(peer_idx);
}

// Session key persistence helpers (delegated to subclass for file I/O)
bool BaseChatMesh::applyLoadedSessionKey(const uint8_t* pub_key_prefix, uint8_t flags, uint16_t nonce,
                                          const uint8_t* session_key, const uint8_t* prev_session_key) {
  return session_keys.applyLoaded(pub_key_prefix, flags, nonce, session_key, prev_session_key);
}

bool BaseChatMesh::getSessionKeyEntry(int idx, uint8_t* pub_key_prefix, uint8_t* flags, uint16_t* nonce,
                                       uint8_t* session_key, uint8_t* prev_session_key) {
  return session_keys.getEntryForSave(idx, pub_key_prefix, flags, nonce, session_key, prev_session_key);
}
