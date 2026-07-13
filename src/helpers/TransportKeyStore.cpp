#include "TransportKeyStore.h"
#include <SHA256.h>
#include <string.h>

uint16_t TransportKey::calcTransportCode(const mesh::Packet* packet) const {
  uint16_t code;
  SHA256 sha;
  sha.resetHMAC(key, sizeof(key));
  uint8_t type = packet->getPayloadType();
  sha.update(&type, 1);
  sha.update(packet->payload, packet->payload_len);
  sha.finalizeHMAC(key, sizeof(key), &code, 2);
  if (code == 0) {     // reserve codes 0000 and FFFF
    code++;
  } else if (code == 0xFFFF) {
    code--;
  }
  return code;
}

bool TransportKey::isNull() const {
  for (int i = 0; i < sizeof(key); i++) {
    if (key[i]) return false;
  }
  return true;  // key is all zeroes
}

void TransportKeyStore::putCache(uint16_t id, const TransportKey& key) {
  if (num_cache < MAX_TKS_ENTRIES) {
    cache_ids[num_cache] = id;
    cache_keys[num_cache] = key;
    num_cache++;
  } else {
    // TODO: evict oldest cache entry
  }
}

void TransportKeyStore::getAutoKeyFor(uint16_t id, const char* name, TransportKey& dest) {
  for (int i = 0; i < num_cache; i++) {  // first, check cache
    if (cache_ids[i] == id) {   // cache hit!
      dest = cache_keys[i];
      return;
    }
  }
  // calc key for publicly-known hashtag region name
  SHA256 sha;
  sha.update(name, strlen(name));
  sha.finalize(&dest.key, sizeof(dest.key));

  putCache(id, dest);
}

int TransportKeyStore::loadKeysFor(uint16_t id, TransportKey keys[], int max_num) {
  int n = 0;
  for (int i = 0; i < num_cache && n < max_num; i++) {  // first, check cache
    if (cache_ids[i] == id) {
      keys[n++] = cache_keys[i];
    }
  }
  if (n > 0) return n;   // cache hit!

  // TODO:  retrieve from difficult-to-copy keystore

  // store in cache (if room)
  for (int i = 0; i < n; i++) {
    putCache(id, keys[i]);
  }
  return n;
}

bool TransportKeyStore::saveKeysFor(uint16_t id, const TransportKey keys[], int num) {
  if (id == 0 || num < 0 || num > MAX_TKS_ENTRIES || (num > 0 && keys == NULL)) {
    return false;
  }

  // Build the replacement off to the side so a full store leaves the current
  // cache untouched. Multiple keys for one private region remain contiguous.
  uint16_t next_ids[MAX_TKS_ENTRIES];
  TransportKey next_keys[MAX_TKS_ENTRIES];
  int next_num = 0;
  for (int i = 0; i < num_cache; i++) {
    if (cache_ids[i] == id) continue;
    next_ids[next_num] = cache_ids[i];
    next_keys[next_num++] = cache_keys[i];
  }
  if (next_num + num > MAX_TKS_ENTRIES) return false;
  for (int i = 0; i < num; i++) {
    next_ids[next_num] = id;
    next_keys[next_num++] = keys[i];
  }

  memcpy(cache_ids, next_ids, sizeof(uint16_t) * next_num);
  memcpy(cache_keys, next_keys, sizeof(TransportKey) * next_num);
  num_cache = next_num;
  return true;
}

bool TransportKeyStore::removeKeys(uint16_t id) {
  if (id == 0) return false;
  int dest = 0;
  for (int i = 0; i < num_cache; i++) {
    if (cache_ids[i] == id) continue;
    cache_ids[dest] = cache_ids[i];
    cache_keys[dest++] = cache_keys[i];
  }
  num_cache = dest;
  return true;
}

bool TransportKeyStore::clear() {
  invalidateCache();
  return true;
}
