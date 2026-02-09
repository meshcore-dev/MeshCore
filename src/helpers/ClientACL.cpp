#include "ClientACL.h"

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
  memset(c, 0, sizeof(*c));
  c->permissions = init_perms;
  c->id = id;
  c->out_path_len = -1;  // initially out_path is unknown
  return c;
}

bool ClientACL::applyPermissions(const mesh::LocalIdentity& self_id, const uint8_t* pubkey, int key_len, uint8_t perms) {
  ClientInfo* c;
  if ((perms & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {  // guest role is not persisted in contacts
    c = getClient(pubkey, key_len);
    if (c == NULL) return false;   // partial pubkey not found

    num_clients--;   // delete from contacts[]
    int i = c - clients;
    while (i < num_clients) {
      clients[i] = clients[i + 1];
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
