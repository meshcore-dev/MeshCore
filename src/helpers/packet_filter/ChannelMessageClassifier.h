#pragma once

#include "BasePacketFilterClassifier.h"

#include <Mesh.h>

#ifndef PACKET_FILTER_CHANNEL_NAME_SIZE
#define PACKET_FILTER_CHANNEL_NAME_SIZE 32
#endif

#ifndef PACKET_FILTER_PATTERN_SIZE
#define PACKET_FILTER_PATTERN_SIZE 80
#endif

class ChannelMessageClassifier : public BasePacketFilterClassifier {
  char _channel_name[PACKET_FILTER_CHANNEL_NAME_SIZE];
  char _pattern[PACKET_FILTER_PATTERN_SIZE];
  uint8_t _channel_hash[PATH_HASH_SIZE];
  uint8_t _channel_secret[PUB_KEY_SIZE];

public:
  ChannelMessageClassifier();

  bool configure(char* args);

  void formatRule(char* dest, size_t dest_len) const;
  bool shouldRepeatPacket(const mesh::Packet* packet);

  static bool handleBlockCommand(char* args, char* reply, size_t reply_len, BlockRuleFn block_rule, void* block_context);
};
