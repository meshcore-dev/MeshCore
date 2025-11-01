#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct ChannelDetails {
  mesh::GroupChannel channel;
  char name[32];
};