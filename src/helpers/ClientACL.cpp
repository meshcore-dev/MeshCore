#include "ClientACL.h"
#include <MeshCore.h>
#include <SHA256.h>
#include <ed_25519.h>

static File openWrite(FILESYSTEM* _fs, const char* filename) {
  #if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    _fs->remove(filename);
    return _fs->open(filename, FILE_O_WRITE);
  #elif defined(RP2040_PLATFORM)
    return _fs->open(filename, "w");
  #else
    return _fs->open(filename, "w", true);
  #endif
}

void ClientACL::load(FILESYSTEM* fs, const mesh::LocalIdentity& self_id) {
  _fs = fs;
  num_clients = 0;
  if (_fs->exists("/s_contacts")) {
  #if defined(RP2040_PLATFORM)
    File file = _fs->open("/s_contacts", "r");
  #else
    File file = _fs->open("/s_contacts");
  #endif
    if (file) {
      bool full = false;
      while (!full) {
        ClientInfo c;
        uint8_t pub_key[32];
        uint8_t unused[2];

        memset(&c, 0, sizeof(c));

        bool success = (file.read(pub_key, 32) == 32);
        success = success && (file.read((uint8_t *) &c.permissions, 1) == 1);
        success = success && (file.read((uint8_t *) &c.extra.room.sync_since, 4) == 4);
        success = success && (file.read(unused, 2) == 2);
        success = success && (file.read((uint8_t *)&c.out_path_len, 1) == 1);
        success = success && (file.read(c.out_path, 64) == 64);
        success = success && (file.read(c.shared_secret, PUB_KEY_SIZE) == PUB_KEY_SIZE); // will be recalculated below

        if (!success) break; // EOF

        c.id = mesh::Identity(pub_key);
        self_id.calcSharedSecret(c.shared_secret, pub_key);  // recalculate shared secrets in case our private key changed
        if (num_clients < MAX_CLIENTS) {
          clients[num_clients++] = c;
        } else {
          full = true;
        }
      }
      file.close();
    }
  }
}

void ClientACL::save(FILESYSTEM* fs, bool (*filter)(ClientInfo*)) {
  _fs = fs;
  File file = openWrite(_fs, "/s_contacts");
  if (file) {
    uint8_t unused[2];
    memset(unused, 0, sizeof(unused));

    for (int i = 0; i < num_clients; i++) {
      auto c = &clients[i];
      if (c->permissions == 0 || (filter && !filter(c))) continue;    // skip deleted entries, or by filter function

      bool success = (file.write(c->id.pub_key, 32) == 32);
      success = success && (file.write((uint8_t *) &c->permissions, 1) == 1);
      success = success && (file.write((uint8_t *) &c->extra.room.sync_since, 4) == 4);
      success = success && (file.write(unused, 2) == 2);
      success = success && (file.write((uint8_t *)&c->out_path_len, 1) == 1);
      success = success && (file.write(c->out_path, 64) == 64);
      success = success && (file.write(c->shared_secret, PUB_KEY_SIZE) == PUB_KEY_SIZE);

      if (!success) break; // write failed
    }
    file.close();
  }
}

bool ClientACL::clear() {
  if (!_fs) return false; // no filesystem, nothing to clear
  if (_fs->exists("/s_contacts")) {
    _fs->remove("/s_contacts");
  }
  memset(clients, 0, sizeof(clients));
  num_clients = 0;
  return true;
}

ClientInfo* ClientACL::getClient(const uint8_t* pubkey, int key_len) {
  for (int i = 0; i < num_clients; i++) {
    if (memcmp(pubkey, clients[i].id.pub_key, key_len) == 0) return &clients[i];  // already known
  }
  return NULL;  // not found
}

ClientInfo* ClientACL::putClient(const mesh::Identity& id, uint8_t init_perms) {
  uint32_t min_time = 0xFFFFFFFF;
  ClientInfo* oldest = &clients[MAX_CLIENTS - 1];
  for (int i = 0; i < num_clients; i++) {
    if (id.matches(clients[i].id)) return &clients[i];  // already known
    if (!clients[i].isAdmin() && clients[i].last_activity < min_time) {
      oldest = &clients[i];
      min_time = oldest->last_activity;
    }
  }

  ClientInfo* c;
  if (num_clients < MAX_CLIENTS) {
    c = &clients[num_clients++];
  } else {
    c = oldest;  // evict least active contact
  }
  int idx = c - clients;
  memset(c, 0, sizeof(*c));
  c->permissions = init_perms;
  c->id = id;
  c->out_path_len = OUT_PATH_UNKNOWN;
  if (_rng) {
    c->aead_nonce = (uint16_t)_rng->nextInt(NONCE_INITIAL_MIN, NONCE_INITIAL_MAX + 1);
  }
  nonce_at_last_persist[idx] = c->aead_nonce;
  return c;
}

uint16_t ClientACL::nextAeadNonceFor(const ClientInfo& client) {
  uint16_t nonce = client.nextAeadNonce();
  if (nonce != 0) {
    int idx = &client - clients;
    if (idx >= 0 && idx < num_clients &&
        (uint16_t)(client.aead_nonce - nonce_at_last_persist[idx]) >= NONCE_PERSIST_INTERVAL) {
      nonce_dirty = true;
    }
  }
  return nonce;
}

void ClientACL::loadNonces() {
  if (!_fs) return;
#if defined(RP2040_PLATFORM)
  File file = _fs->open("/s_nonces", "r");
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File file = _fs->open("/s_nonces", FILE_O_READ);
#else
  File file = _fs->open("/s_nonces", "r", false);
#endif
  if (file) {
    uint8_t rec[6];  // 4-byte pub_key prefix + 2-byte nonce
    while (file.read(rec, 6) == 6) {
      uint16_t nonce;
      memcpy(&nonce, &rec[4], 2);
      for (int i = 0; i < num_clients; i++) {
        if (memcmp(clients[i].id.pub_key, rec, 4) == 0) {
          clients[i].aead_nonce = nonce;
          break;
        }
      }
    }
    file.close();
  }
}

void ClientACL::saveNonces() {
  if (!_fs) return;
  File file = openWrite(_fs, "/s_nonces");
  if (file) {
    for (int i = 0; i < num_clients; i++) {
      file.write(clients[i].id.pub_key, 4);
      file.write((uint8_t*)&clients[i].aead_nonce, 2);
      nonce_at_last_persist[i] = clients[i].aead_nonce;
    }
    file.close();
    nonce_dirty = false;
  }
}

void ClientACL::finalizeNonceLoad(bool needs_bump) {
  for (int i = 0; i < num_clients; i++) {
    if (needs_bump) {
      uint16_t old = clients[i].aead_nonce;
      clients[i].aead_nonce += NONCE_BOOT_BUMP;
      if (clients[i].aead_nonce == 0) clients[i].aead_nonce = 1;
      if (clients[i].aead_nonce < old) {
        MESH_DEBUG_PRINTLN("AEAD nonce wrapped after boot bump for client: %02x%02x%02x%02x",
          clients[i].id.pub_key[0], clients[i].id.pub_key[1],
          clients[i].id.pub_key[2], clients[i].id.pub_key[3]);
      }
    }
    nonce_at_last_persist[i] = clients[i].aead_nonce;
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

bool ClientACL::applyPermissions(const mesh::LocalIdentity& self_id, const uint8_t* pubkey, int key_len, uint8_t perms) {
  ClientInfo* c;
  if ((perms & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {  // guest role is not persisted in contacts
    c = getClient(pubkey, key_len);
    if (c == NULL) return false;   // partial pubkey not found

    removeSessionKey(c->id.pub_key);  // also remove session key if any

    num_clients--;   // delete from contacts[]
    int i = c - clients;
    while (i < num_clients) {
      clients[i] = clients[i + 1];
      nonce_at_last_persist[i] = nonce_at_last_persist[i + 1];
      i++;
    }
  } else {
    if (key_len < PUB_KEY_SIZE) return false;   // need complete pubkey when adding/modifying

    mesh::Identity id(pubkey);
    c = putClient(id, 0);

    c->permissions = perms;  // update their permissions
    self_id.calcSharedSecret(c->shared_secret, pubkey);
  }
  return true;
}

// --- Session key support (Phase 2) ---

int ClientACL::handleSessionKeyInit(const ClientInfo* client, const uint8_t* ephemeral_pub_A, uint8_t* reply_buf, mesh::RNG* rng) {
  // 1. Generate ephemeral keypair B
  uint8_t seed[SEED_SIZE];
  rng->random(seed, SEED_SIZE);
  uint8_t ephemeral_pub_B[PUB_KEY_SIZE];
  uint8_t ephemeral_prv_B[PRV_KEY_SIZE];
  ed25519_create_keypair(ephemeral_pub_B, ephemeral_prv_B, seed);
  memset(seed, 0, SEED_SIZE);

  // 2. Compute ephemeral_secret via X25519
  uint8_t ephemeral_secret[PUB_KEY_SIZE];
  ed25519_key_exchange(ephemeral_secret, ephemeral_pub_A, ephemeral_prv_B);
  memset(ephemeral_prv_B, 0, PRV_KEY_SIZE);

  // 3. Derive session_key = HMAC-SHA256(static_shared_secret, ephemeral_secret)
  uint8_t new_session_key[SESSION_KEY_SIZE];
  {
    SHA256 sha;
    sha.resetHMAC(client->shared_secret, PUB_KEY_SIZE);
    sha.update(ephemeral_secret, PUB_KEY_SIZE);
    sha.finalizeHMAC(client->shared_secret, PUB_KEY_SIZE, new_session_key, SESSION_KEY_SIZE);
  }
  memset(ephemeral_secret, 0, PUB_KEY_SIZE);

  // 4. Store in pool (dual-decode: new key active, old key still valid)
  auto entry = allocateSessionKey(client->id.pub_key);
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
  saveSessionKeys();

  // 6. Write ephemeral_pub_B to reply
  memcpy(reply_buf, ephemeral_pub_B, PUB_KEY_SIZE);
  return PUB_KEY_SIZE;
}

const uint8_t* ClientACL::getSessionKey(const uint8_t* pub_key) {
  auto entry = findSessionKey(pub_key);
  if (entry && (entry->state == SESSION_STATE_ACTIVE || entry->state == SESSION_STATE_DUAL_DECODE)) {
    return entry->session_key;
  }
  return nullptr;
}

const uint8_t* ClientACL::getPrevSessionKey(const uint8_t* pub_key) {
  auto entry = findSessionKey(pub_key);
  if (entry && entry->state == SESSION_STATE_DUAL_DECODE) {
    return entry->prev_session_key;
  }
  return nullptr;
}

const uint8_t* ClientACL::getEncryptionKey(const ClientInfo& client) {
  auto entry = findSessionKey(client.id.pub_key);
  if (entry && (entry->state == SESSION_STATE_ACTIVE || entry->state == SESSION_STATE_DUAL_DECODE)
      && entry->sends_since_last_recv < SESSION_KEY_STALE_THRESHOLD
      && entry->nonce < 65535) {
    return entry->session_key;
  }
  return client.shared_secret;
}

uint16_t ClientACL::getEncryptionNonce(const ClientInfo& client) {
  auto entry = findSessionKey(client.id.pub_key);
  if (entry && (entry->state == SESSION_STATE_ACTIVE || entry->state == SESSION_STATE_DUAL_DECODE)
      && entry->sends_since_last_recv < SESSION_KEY_STALE_THRESHOLD
      && entry->nonce < 65535) {
    ++entry->nonce;
    if (entry->sends_since_last_recv < 255) entry->sends_since_last_recv++;
    _session_keys_dirty = true;
    return entry->nonce;
  }
  // Progressive fallback: keep incrementing counter even when not using session key
  if (entry && entry->sends_since_last_recv < 255) {
    entry->sends_since_last_recv++;
    if (entry->sends_since_last_recv >= SESSION_KEY_ABANDON_THRESHOLD) {
      int idx = &client - clients;
      if (idx >= 0 && idx < num_clients)
        clients[idx].flags &= ~CONTACT_FLAG_AEAD;
      removeSessionKey(client.id.pub_key);
      saveSessionKeys();
      return 0;  // ECB
    }
    if (entry->sends_since_last_recv >= SESSION_KEY_ECB_THRESHOLD) {
      return 0;  // ECB
    }
  }
  return nextAeadNonceFor(client);
}

void ClientACL::onSessionConfirmed(const uint8_t* pub_key) {
  auto entry = findSessionKey(pub_key);
  if (entry) {
    if (entry->state == SESSION_STATE_DUAL_DECODE) {
      memset(entry->prev_session_key, 0, SESSION_KEY_SIZE);
      entry->state = SESSION_STATE_ACTIVE;
      saveSessionKeys();
    }
    entry->sends_since_last_recv = 0;
  }
}

// --- Peer-index forwarding helpers ---

ClientInfo* ClientACL::resolveClient(int peer_idx, const int* matching_indexes) {
  int i = matching_indexes[peer_idx];
  if (i >= 0 && i < num_clients) return &clients[i];
  return nullptr;
}

uint16_t ClientACL::peerNextAeadNonce(int peer_idx, const int* matching_indexes) {
  auto* c = resolveClient(peer_idx, matching_indexes);
  return c ? nextAeadNonceFor(*c) : 0;
}

const uint8_t* ClientACL::peerSessionKey(int peer_idx, const int* matching_indexes) {
  auto* c = resolveClient(peer_idx, matching_indexes);
  return c ? getSessionKey(c->id.pub_key) : nullptr;
}

const uint8_t* ClientACL::peerPrevSessionKey(int peer_idx, const int* matching_indexes) {
  auto* c = resolveClient(peer_idx, matching_indexes);
  return c ? getPrevSessionKey(c->id.pub_key) : nullptr;
}

void ClientACL::peerSessionKeyDecryptSuccess(int peer_idx, const int* matching_indexes) {
  auto* c = resolveClient(peer_idx, matching_indexes);
  if (c) onSessionConfirmed(c->id.pub_key);
}

const uint8_t* ClientACL::peerEncryptionKey(int peer_idx, const int* matching_indexes, const uint8_t* fallback) {
  auto* c = resolveClient(peer_idx, matching_indexes);
  return c ? getEncryptionKey(*c) : fallback;
}

uint16_t ClientACL::peerEncryptionNonce(int peer_idx, const int* matching_indexes) {
  auto* c = resolveClient(peer_idx, matching_indexes);
  return c ? getEncryptionNonce(*c) : 0;
}

// --- Flash-backed session key wrappers ---

static File openReadACL(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "r");
#else
  return fs->open(filename, "r", false);
#endif
}

bool ClientACL::loadSessionKeyRecordFromFlash(const uint8_t* prefix,
    uint8_t* flags, uint16_t* nonce, uint8_t* session_key, uint8_t* prev_session_key) {
  if (!_fs) return false;
  File f = openReadACL(_fs, "/s_sess_keys");
  if (!f) return false;
  while (true) {
    uint8_t rec[SESSION_KEY_RECORD_MIN_SIZE];
    if (f.read(rec, SESSION_KEY_RECORD_MIN_SIZE) != SESSION_KEY_RECORD_MIN_SIZE) break;
    uint8_t rec_flags = rec[4];
    bool has_prev = (rec_flags & SESSION_FLAG_PREV_VALID);
    if (memcmp(rec, prefix, 4) == 0) {
      *flags = rec_flags;
      memcpy(nonce, &rec[5], 2);
      memcpy(session_key, &rec[7], SESSION_KEY_SIZE);
      if (has_prev) {
        if (f.read(prev_session_key, SESSION_KEY_SIZE) != SESSION_KEY_SIZE) break;
      } else {
        memset(prev_session_key, 0, SESSION_KEY_SIZE);
      }
      f.close();
      return true;
    }
    // Skip prev_key if present
    if (has_prev) {
      uint8_t skip[SESSION_KEY_SIZE];
      if (f.read(skip, SESSION_KEY_SIZE) != SESSION_KEY_SIZE) break;
    }
  }
  f.close();
  return false;
}

SessionKeyEntry* ClientACL::findSessionKey(const uint8_t* pub_key) {
  auto entry = session_keys.findByPrefix(pub_key);
  if (entry) return entry;

  // Cache miss — try flash
  uint8_t flags; uint16_t nonce;
  uint8_t sk[SESSION_KEY_SIZE], psk[SESSION_KEY_SIZE];
  if (!loadSessionKeyRecordFromFlash(pub_key, &flags, &nonce, sk, psk)) return nullptr;

  // Save dirty evictee before overwriting
  if (session_keys.isFull() && _session_keys_dirty) {
    saveSessionKeys();
  }
  session_keys.applyLoaded(pub_key, flags, nonce, sk, psk);
  return session_keys.findByPrefix(pub_key);
}

SessionKeyEntry* ClientACL::allocateSessionKey(const uint8_t* pub_key) {
  auto entry = findSessionKey(pub_key);
  if (entry) return entry;

  // Not found anywhere — save dirty evictee before allocating
  if (session_keys.isFull() && _session_keys_dirty) {
    saveSessionKeys();
  }
  return session_keys.allocate(pub_key);
}

void ClientACL::removeSessionKey(const uint8_t* pub_key) {
  session_keys.remove(pub_key);
}

void ClientACL::loadSessionKeys() {
  if (!_fs) return;
  File file = openReadACL(_fs, "/s_sess_keys");
  if (!file) return;
  while (true) {
    uint8_t rec[SESSION_KEY_RECORD_MIN_SIZE];
    if (file.read(rec, SESSION_KEY_RECORD_MIN_SIZE) != SESSION_KEY_RECORD_MIN_SIZE) break;
    uint8_t flags = rec[4];
    uint16_t nonce;
    memcpy(&nonce, &rec[5], 2);
    uint8_t prev_key[SESSION_KEY_SIZE];
    if (flags & SESSION_FLAG_PREV_VALID) {
      if (file.read(prev_key, SESSION_KEY_SIZE) != SESSION_KEY_SIZE) break;
    } else {
      memset(prev_key, 0, SESSION_KEY_SIZE);
    }
    session_keys.applyLoaded(rec, flags, nonce, &rec[7], prev_key);
  }
  file.close();
}

void ClientACL::saveSessionKeys() {
  if (!_fs) return;

  // 1. Read old flash file into buffer (variable-length records)
  uint8_t old_buf[MAX_SESSION_KEYS_FLASH * SESSION_KEY_RECORD_SIZE];
  int old_len = 0;
  File rf = openReadACL(_fs, "/s_sess_keys");
  if (rf) {
    while (true) {
      if (old_len + SESSION_KEY_RECORD_MIN_SIZE > (int)sizeof(old_buf)) break;
      if (rf.read(&old_buf[old_len], SESSION_KEY_RECORD_MIN_SIZE) != SESSION_KEY_RECORD_MIN_SIZE) break;
      uint8_t flags = old_buf[old_len + 4];
      int rec_len = SESSION_KEY_RECORD_MIN_SIZE;
      if (flags & SESSION_FLAG_PREV_VALID) {
        if (old_len + SESSION_KEY_RECORD_SIZE > (int)sizeof(old_buf)) break;
        if (rf.read(&old_buf[old_len + SESSION_KEY_RECORD_MIN_SIZE], SESSION_KEY_SIZE) != SESSION_KEY_SIZE) break;
        rec_len = SESSION_KEY_RECORD_SIZE;
      }
      old_len += rec_len;
    }
    rf.close();
  }

  // 2. Write merged file
  File wf = openWrite(_fs, "/s_sess_keys");
  if (!wf) return;

  // Write kept old records (variable-length)
  int pos = 0;
  while (pos + SESSION_KEY_RECORD_MIN_SIZE <= old_len) {
    uint8_t* rec = &old_buf[pos];
    uint8_t flags = rec[4];
    int rec_len = (flags & SESSION_FLAG_PREV_VALID) ? SESSION_KEY_RECORD_SIZE : SESSION_KEY_RECORD_MIN_SIZE;
    if (pos + rec_len > old_len) break;
    if (!session_keys.hasPrefix(rec) && !session_keys.isRemoved(rec)) {
      wf.write(rec, rec_len);
    }
    pos += rec_len;
  }
  // Write current RAM entries (variable-length)
  for (int i = 0; i < session_keys.getCount(); i++) {
    uint8_t pub_key_prefix[4];
    uint8_t flags;
    uint16_t nonce;
    uint8_t session_key[SESSION_KEY_SIZE];
    uint8_t prev_session_key[SESSION_KEY_SIZE];
    if (session_keys.getEntryForSave(i, pub_key_prefix, &flags, &nonce, session_key, prev_session_key)) {
      wf.write(pub_key_prefix, 4);
      wf.write(&flags, 1);
      wf.write((uint8_t*)&nonce, 2);
      wf.write(session_key, SESSION_KEY_SIZE);
      if (flags & SESSION_FLAG_PREV_VALID) {
        wf.write(prev_session_key, SESSION_KEY_SIZE);
      }
    }
  }
  wf.close();
  _session_keys_dirty = false;
  session_keys.clearRemoved();
}
