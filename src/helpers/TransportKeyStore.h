#pragma once

#include <Arduino.h>   // needed for PlatformIO
#include <Packet.h>
#include <helpers/IdentityStore.h>

struct TransportKey {
  uint8_t key[16];

  uint16_t calcTransportCode(const mesh::Packet* packet) const;
  bool isNull() const;
};

#define MAX_TKS_ENTRIES   16

class TransportKeyStore {
  uint16_t     cache_ids[MAX_TKS_ENTRIES];
  TransportKey cache_keys[MAX_TKS_ENTRIES];
  int num_cache;

  void putCache(uint16_t id, const TransportKey& key);
  void invalidateCache() { num_cache = 0; }

public:
  TransportKeyStore() { num_cache = 0; }
  void getAutoKeyFor(uint16_t id, const char* name, TransportKey& dest);
  int loadKeysFor(uint16_t id, TransportKey keys[], int max_num);
  bool saveKeysFor(uint16_t id, const TransportKey keys[], int num);
  bool removeKeys(uint16_t id);
  bool clear();
};
