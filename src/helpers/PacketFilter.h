#pragma once

#include <Arduino.h>
#include <helpers/IdentityStore.h>
#include <helpers/packet_filter/PacketFilterRule.h>

#ifndef PACKET_FILTER_MAX_RULES
#define PACKET_FILTER_MAX_RULES 20
#endif

class PacketFilter {
  PacketFilterRule _rules[PACKET_FILTER_MAX_RULES];
  uint8_t _rule_count;
  uint32_t _filtered_count;
  mesh::RNG* _rng;

public:
  PacketFilter(mesh::RNG* rng = nullptr);

  bool shouldRepeatPacket(const mesh::Packet* packet);
  bool block(const char* classifier_name, char* args);
  bool deleteRule(uint8_t rule_index);
  void clear();

  uint32_t getFilteredCount() const { return _filtered_count; }
  void resetStats() { _filtered_count = 0; }

  uint8_t getRuleCount() const { return _rule_count; }
  void formatRules(char* reply, size_t reply_len, uint8_t page = 1) const;
  void handleBlockCommand(FILESYSTEM* fs, const char* path, char* command, char* reply, size_t reply_len);

  bool load(FILESYSTEM* fs, const char* path);
  bool save(FILESYSTEM* fs, const char* path) const;
};
