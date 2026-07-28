#pragma once

#include "BasePacketFilterClassifier.h"

#include <Mesh.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/SimpleBloomFilter.h>

#ifndef PACKET_FILTER_ADVERT_BLOOM_BITS
#define PACKET_FILTER_ADVERT_BLOOM_BITS 1024
#endif

#ifndef PACKET_FILTER_ADVERT_BLOOM_STORAGE_BYTES
#define PACKET_FILTER_ADVERT_BLOOM_STORAGE_BYTES ((PACKET_FILTER_ADVERT_BLOOM_BITS + 7) / 8)
#endif

#ifndef PACKET_FILTER_ADVERT_MAX_PER_PERIOD
#define PACKET_FILTER_ADVERT_MAX_PER_PERIOD 4
#endif

#ifndef PACKET_FILTER_ADVERT_BLOOM_MAX_FILL_PERCENT
#define PACKET_FILTER_ADVERT_BLOOM_MAX_FILL_PERCENT 75
#endif

#ifndef PACKET_FILTER_ADVERT_BLOOM_HASH_COUNT
#define PACKET_FILTER_ADVERT_BLOOM_HASH_COUNT 4
#endif

#ifndef PACKET_FILTER_ADVERT_BLOOM_SALT_BYTES
#define PACKET_FILTER_ADVERT_BLOOM_SALT_BYTES 16
#endif

class AdvertRateLimitClassifier : public BasePacketFilterClassifier {
  bool _saturated;
  uint8_t _advert_type;
  uint16_t _period_minutes;
  uint8_t _max_per_period;
  uint32_t _window_id;
  mesh::RNG* _rng;
  uint8_t _salts[PACKET_FILTER_ADVERT_MAX_PER_PERIOD][PACKET_FILTER_ADVERT_BLOOM_SALT_BYTES];
  uint8_t _filter_storage[PACKET_FILTER_ADVERT_MAX_PER_PERIOD][PACKET_FILTER_ADVERT_BLOOM_STORAGE_BYTES];
  SimpleBloomFilter _filters[PACKET_FILTER_ADVERT_MAX_PER_PERIOD];

  void generateSalts();
  void clearFilters();
  void configureFilters();
  void resetWindowIfNeeded();
  bool filterContains(uint8_t filter_idx, const uint8_t* cert) const;
  void addToFilter(uint8_t filter_idx, const uint8_t* cert);
  bool isFilterSaturated(uint8_t filter_idx) const;
  bool configureParsed(uint8_t advert_type, uint16_t period_minutes, uint8_t max_per_period);

public:
  AdvertRateLimitClassifier(mesh::RNG* rng = nullptr);

  bool configure(char* args);

  void formatRule(char* dest, size_t dest_len) const;
  bool shouldRepeatPacket(const mesh::Packet* packet);

  static bool handleBlockCommand(char* args, char* reply, size_t reply_len, BlockRuleFn block_rule, void* block_context);

private:
  static bool parseAdvertType(const char* name, uint8_t* advert_type);
  static const char* advertTypeName(uint8_t advert_type);
  static const char* formatAdvertType(uint8_t advert_type, char* dest, size_t dest_len);
};
