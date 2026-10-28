#include "ClientACL.h"
#include "ConfigSerializer.h"

struct SaveAclCtx {
  ClientACL* acl;
  bool (*filter)(ClientInfo*);
};

static bool writeAclBody(File& file, void* ctx) {
  SaveAclCtx* c = (SaveAclCtx*) ctx;
  uint8_t unused[2];
  memset(unused, 0, sizeof(unused));

  for (int i = 0; i < c->acl->getNumClients(); i++) {
    auto client = c->acl->getClientByIdx(i);
    if (client->permissions == 0 || (c->filter && !c->filter(client))) continue;

    bool success = (file.write(client->id.pub_key, 32) == 32);
    success = success && (file.write((uint8_t*) &client->permissions, 1) == 1);
    success = success && (file.write((uint8_t*) &client->extra.room.sync_since, 4) == 4);
    success = success && (file.write(unused, 2) == 2);
    success = success && (file.write((uint8_t*) &client->out_path_len, 1) == 1);
    success = success && (file.write(client->out_path, 64) == 64);
    success = success && (file.write(client->shared_secret, PUB_KEY_SIZE) == PUB_KEY_SIZE);
    if (!success) return false;
  }
  return true;
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
  SaveAclCtx ctx = {this, filter};
  writeFileAtomic(_fs, "/s_contacts", "/.s_contacts.new", writeAclBody, &ctx);
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
  c->out_path_len = OUT_PATH_UNKNOWN;
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
