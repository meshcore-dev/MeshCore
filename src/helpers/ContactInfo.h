#pragma once

#include <Arduino.h>
#include <Mesh.h>

class Adafruit_LittleFS;

#define OUT_PATH_UNKNOWN   0xFF

// chunk constants
#define CHUNK_MAGIC           0xC047
#define CHUNK_VERSION         1
#define SLOTS_PER_CHUNK       25
#define MAX_CHUNKS            14
#define CONTACT_RECORD_SIZE   152   // on-disk size of one contact

struct ChunkHeader {
  uint16_t magic;                          // 2 bytes — 0xC047
  uint8_t  version;                        // 1 byte  — schema version
  uint8_t  chunk_index;                    // 1 byte  — 0 to MAX_CHUNKS-1
  uint8_t  valid_count;                    // 1 byte  — redundant check vs tombstones
  uint8_t  tombstones[SLOTS_PER_CHUNK];    // 25 bytes — 0=valid, 1=deleted
  uint8_t  sha256[32];                     // 32 bytes — integrity hash
  uint8_t  reserved[90];                   // 90 bytes — future use
  // Total: 152 bytes (matches CONTACT_RECORD_SIZE)
};

struct ContactInfo {
  mesh::Identity id;
  char name[32];
  uint8_t type;   // on of ADV_TYPE_*
  uint8_t flags;
  uint8_t out_path_len;
  mutable bool shared_secret_valid; // flag to indicate if shared_secret has been calculated
  uint8_t out_path[MAX_PATH_SIZE];
  uint32_t last_advert_timestamp;   // by THEIR clock
  uint32_t lastmod;  // by OUR clock
  int32_t gps_lat, gps_lon;    // 6 dec places
  uint32_t sync_since;
  uint8_t chunk_index;    // 0xFF = not yet assigned, tracks which chunk the contact is stored in
  uint8_t slot_index;     // 0xFF = not yet assigned, tracks which slot in the chunk the contact is stored in

  const uint8_t* getSharedSecret(const mesh::LocalIdentity& self_id) const {
    if (!shared_secret_valid) {
      self_id.calcSharedSecret(shared_secret, id.pub_key);
      shared_secret_valid = true;
    }
    return shared_secret;
  }

private:
  mutable uint8_t shared_secret[PUB_KEY_SIZE];
};
