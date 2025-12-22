#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ContactInfo {
  mesh::Identity id;
  char name[32];
  uint8_t type;   // on of ADV_TYPE_*
  uint8_t flags;
  int8_t out_path_len;
  mutable bool shared_secret_valid; // flag to indicate if shared_secret has been calculated
  uint8_t out_path[MAX_PATH_SIZE];
  uint32_t last_advert_timestamp;   // by THEIR clock
  uint32_t lastmod;  // by OUR clock
  int32_t gps_lat, gps_lon;    // 6 dec places
  uint32_t sync_since;

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
