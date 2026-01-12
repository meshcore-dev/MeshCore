#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ContactInfo {
  mesh::Identity id;
  char name[32];
  uint8_t type;   // one of ADV_TYPE_*
  uint8_t flags;
  int8_t out_path_len;
  uint8_t out_path[MAX_PATH_SIZE];
  uint32_t last_advert_timestamp;   // by THEIR clock
  uint8_t shared_secret[PUB_KEY_SIZE];
  uint32_t lastmod;  // by OUR clock
  int32_t gps_lat, gps_lon;    // 6 dec places
  uint32_t sync_since;
};
