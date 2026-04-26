#include "TransportKeyStore.h"
#include <SHA256.h>

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
  invalidateCache();

  // TODO: update hardware keystore

  return false;  // failed
}

bool TransportKeyStore::removeKeys(uint16_t id) {
  invalidateCache();

  // TODO: remove from hardware keystore

  return false;  // failed
}

bool TransportKeyStore::clear() {
  invalidateCache();

  // TODO: clear hardware keystore

  return false;  // failed
}
