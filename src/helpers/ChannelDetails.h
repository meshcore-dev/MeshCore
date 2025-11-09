#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ChannelFlags {
    bool noStore : 1;
    uint8_t reserved : 7;  // remaining 7 bits unused
};

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
  ChannelFlags flags;
};