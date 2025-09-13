#pragma once

#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <helpers/IdentityStore.h>

#define PERM_ACL_ROLE_MASK     3   // lower 2 bits
#define PERM_ACL_GUEST         0
#define PERM_ACL_READ_ONLY     1
#define PERM_ACL_READ_WRITE    2
#define PERM_ACL_ADMIN         3

struct ClientInfo {
  mesh::Identity id;
  uint8_t permissions;
  int8_t out_path_len;
  uint8_t out_path[MAX_PATH_SIZE];
  uint8_t shared_secret[PUB_KEY_SIZE];
  uint32_t last_timestamp;   // by THEIR clock  (transient)
  uint32_t last_activity;    // by OUR clock    (transient)

  bool isAdmin() const { return (permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_ADMIN; }
};

#ifndef MAX_CLIENTS
  #define MAX_CLIENTS           20
#endif

class ClientACL {
  ClientInfo clients[MAX_CLIENTS];
  int num_clients;

public:
  ClientACL() { 
    memset(clients, 0, sizeof(clients));
    num_clients = 0;
  }
  void load(FILESYSTEM* _fs);
  void save(FILESYSTEM* _fs);

  ClientInfo* getClient(const uint8_t* pubkey, int key_len);
  ClientInfo* putClient(const mesh::Identity& id, uint8_t init_perms);
  bool applyPermissions(const mesh::LocalIdentity& self_id, const uint8_t* pubkey, int key_len, uint8_t perms);

  int getNumClients() const { return num_clients; }
  ClientInfo* getClientByIdx(int idx) { return &clients[idx]; }
};
