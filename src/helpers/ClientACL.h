#pragma once

#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <helpers/IdentityStore.h>
#include <helpers/SessionKeyPool.h>

#define PERM_ACL_ROLE_MASK     3   // lower 2 bits
#define PERM_ACL_GUEST         0
#define PERM_ACL_READ_ONLY     1
#define PERM_ACL_READ_WRITE    2
#define PERM_ACL_ADMIN         3

#define OUT_PATH_UNKNOWN  0xFF

struct ClientInfo {
  mesh::Identity id;
  uint8_t permissions;
  uint8_t flags;                    // transient — includes CONTACT_FLAG_AEAD
  mutable uint16_t aead_nonce;      // transient — per-peer nonce counter
  uint8_t out_path_len;
  uint8_t out_path[MAX_PATH_SIZE];
  uint8_t shared_secret[PUB_KEY_SIZE];
  uint32_t last_timestamp;   // by THEIR clock  (transient)
  uint32_t last_activity;    // by OUR clock    (transient)
  union  {
    struct {
      uint32_t sync_since;  // sync messages SINCE this timestamp (by OUR clock)
      uint32_t pending_ack;
      uint32_t push_post_timestamp;
      unsigned long ack_timeout;
      uint8_t  push_failures;
    } room;
  } extra;

  uint16_t nextAeadNonce() const {
    if (flags & CONTACT_FLAG_AEAD) {
      if (++aead_nonce == 0) ++aead_nonce;  // skip 0 (means ECB)
      return aead_nonce;
    }
    return 0;
  }
  bool isAdmin() const { return (permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_ADMIN; }
};

#ifndef MAX_CLIENTS
  #define MAX_CLIENTS           20
#endif

class ClientACL {
  FILESYSTEM* _fs;
  ClientInfo clients[MAX_CLIENTS];
  int num_clients;

  // Nonce persistence state (parallel to clients[])
  uint16_t nonce_at_last_persist[MAX_CLIENTS];
  bool nonce_dirty;
  bool _session_keys_dirty;
  mesh::RNG* _rng;

  // Session key pool (Phase 2)
  SessionKeyPool session_keys;

public:
  ClientACL() {
    memset(clients, 0, sizeof(clients));
    memset(nonce_at_last_persist, 0, sizeof(nonce_at_last_persist));
    num_clients = 0;
    nonce_dirty = false;
    _session_keys_dirty = false;
    _rng = NULL;
  }
  void load(FILESYSTEM* _fs, const mesh::LocalIdentity& self_id);
  void save(FILESYSTEM* _fs, bool (*filter)(ClientInfo*)=NULL);
  bool clear();

  ClientInfo* getClient(const uint8_t* pubkey, int key_len);
  ClientInfo* putClient(const mesh::Identity& id, uint8_t init_perms);
  bool applyPermissions(const mesh::LocalIdentity& self_id, const uint8_t* pubkey, int key_len, uint8_t perms);

  int getNumClients() const { return num_clients; }
  ClientInfo* getClientByIdx(int idx) { return &clients[idx]; }
  int getSessionKeyCount() const { return session_keys.getCount(); }

  // AEAD nonce persistence
  void setRNG(mesh::RNG* rng) { _rng = rng; }
  uint16_t nextAeadNonceFor(const ClientInfo& client);
  void loadNonces();
  void saveNonces();
  void finalizeNonceLoad(bool needs_bump);
  bool isNonceDirty() const { return nonce_dirty; }
  void clearNonceDirty() {
    for (int i = 0; i < num_clients; i++) nonce_at_last_persist[i] = clients[i].aead_nonce;
    nonce_dirty = false;
  }

  // Session key support (Phase 2)
  int handleSessionKeyInit(const ClientInfo* client, const uint8_t* ephemeral_pub_A, uint8_t* reply_buf, mesh::RNG* rng);
  const uint8_t* getSessionKey(const uint8_t* pub_key);
  const uint8_t* getPrevSessionKey(const uint8_t* pub_key);
  const uint8_t* getEncryptionKey(const ClientInfo& client);
  uint16_t getEncryptionNonce(const ClientInfo& client);
  void onSessionConfirmed(const uint8_t* pub_key);
  bool isSessionKeysDirty() const { return _session_keys_dirty; }
  void loadSessionKeys();
  void saveSessionKeys();

  // Peer-index forwarding helpers for server-side Mesh overrides.
  // These resolve peer_idx → ClientInfo via matching_indexes[], then delegate
  // to the corresponding method above.  Eliminates repeated boilerplate in
  // repeater/room/sensor examples.
  ClientInfo* resolveClient(int peer_idx, const int* matching_indexes);
  uint16_t peerNextAeadNonce(int peer_idx, const int* matching_indexes);
  const uint8_t* peerSessionKey(int peer_idx, const int* matching_indexes);
  const uint8_t* peerPrevSessionKey(int peer_idx, const int* matching_indexes);
  void peerSessionKeyDecryptSuccess(int peer_idx, const int* matching_indexes);
  const uint8_t* peerEncryptionKey(int peer_idx, const int* matching_indexes, const uint8_t* fallback);
  uint16_t peerEncryptionNonce(int peer_idx, const int* matching_indexes);
};
