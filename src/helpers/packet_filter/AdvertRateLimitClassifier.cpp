#include "helpers/packet_filter/AdvertRateLimitClassifier.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

AdvertRateLimitClassifier::AdvertRateLimitClassifier(mesh::RNG* rng) {
  _rng = rng;
  _saturated = false;
  _advert_type = ADV_TYPE_REPEATER;
  _period_minutes = 0;
  _max_per_period = 0;
  _window_id = 0xFFFFFFFFUL;
  memset(_salts, 0, sizeof(_salts));
  configureFilters();
}

void AdvertRateLimitClassifier::generateSalts() {
  for (uint8_t i = 0; i < PACKET_FILTER_ADVERT_MAX_PER_PERIOD; i++) {
    if (_rng != nullptr) {
      _rng->random(_salts[i], sizeof(_salts[i]));
    }
    _filters[i].setSalt(_salts[i], sizeof(_salts[i]));
  }
}

void AdvertRateLimitClassifier::clearFilters() {
  for (uint8_t i = 0; i < PACKET_FILTER_ADVERT_MAX_PER_PERIOD; i++) {
    _filters[i].clear();
  }
  _saturated = false;
}

void AdvertRateLimitClassifier::configureFilters() {
  for (uint8_t i = 0; i < PACKET_FILTER_ADVERT_MAX_PER_PERIOD; i++) {
    _filters[i].begin(_filter_storage[i], sizeof(_filter_storage[i]), PACKET_FILTER_ADVERT_BLOOM_BITS, PACKET_FILTER_ADVERT_BLOOM_HASH_COUNT);
    _filters[i].setSalt(_salts[i], sizeof(_salts[i]));
  }
  clearFilters();
}

bool AdvertRateLimitClassifier::configureParsed(uint8_t advert_type, uint16_t period_minutes, uint8_t max_per_period) {
  if (period_minutes == 0 || max_per_period == 0) return false;
  if (max_per_period > PACKET_FILTER_ADVERT_MAX_PER_PERIOD) return false;
  if (advert_type > 0x0F) return false;
  if (PACKET_FILTER_ADVERT_BLOOM_BITS == 0 || PACKET_FILTER_ADVERT_BLOOM_HASH_COUNT == 0) return false;

  _advert_type = advert_type;
  _period_minutes = period_minutes;
  _max_per_period = max_per_period;
  _window_id = 0xFFFFFFFFUL;
  generateSalts();
  clearFilters();
  return true;
}

bool AdvertRateLimitClassifier::configure(char* args) {
  char* advert_type = nextToken(args);
  char* period = nextToken(args);
  char* max_count = nextToken(args);
  char* extra = nextToken(args);

  unsigned long period_minutes = 0;
  unsigned long max_per_period = 1;
  uint8_t parsed_type;
  if (!parseAdvertType(advert_type, &parsed_type)) return false;
  if (!parseUnsigned(period, 1, 65535UL, &period_minutes)) return false;
  if (max_count != NULL && !parseUnsigned(max_count, 1, PACKET_FILTER_ADVERT_MAX_PER_PERIOD, &max_per_period)) return false;
  if (extra != NULL) return false;

  return configureParsed(parsed_type, (uint16_t)period_minutes, (uint8_t)max_per_period);
}

void AdvertRateLimitClassifier::formatRule(char* dest, size_t dest_len) const {
  char type_name[4];
  snprintf(
    dest,
    dest_len,
    "advert %s %u %u",
    formatAdvertType(_advert_type, type_name, sizeof(type_name)),
    _period_minutes,
    _max_per_period
  );
}

void AdvertRateLimitClassifier::resetWindowIfNeeded() {
  uint32_t period_ms = ((uint32_t)_period_minutes) * 60UL * 1000UL;
  uint32_t window_id = period_ms == 0 ? 0 : millis() / period_ms;
  if (window_id != _window_id) {
    generateSalts();
    clearFilters();
    _window_id = window_id;
  }
}

bool AdvertRateLimitClassifier::filterContains(uint8_t filter_idx, const uint8_t* cert) const {
  return _filters[filter_idx].contains(cert, PUB_KEY_SIZE);
}

void AdvertRateLimitClassifier::addToFilter(uint8_t filter_idx, const uint8_t* cert) {
  _filters[filter_idx].add(cert, PUB_KEY_SIZE);
  if (isFilterSaturated(filter_idx)) _saturated = true;
}

bool AdvertRateLimitClassifier::isFilterSaturated(uint8_t filter_idx) const {
  return _filters[filter_idx].isSaturated(PACKET_FILTER_ADVERT_BLOOM_MAX_FILL_PERCENT);
}

bool AdvertRateLimitClassifier::shouldRepeatPacket(const mesh::Packet* packet) {
  if (packet == NULL || packet->getPayloadType() != PAYLOAD_TYPE_ADVERT) return true;

  const uint16_t app_data_offset = PUB_KEY_SIZE + 4 + SIGNATURE_SIZE;
  if (packet->payload_len <= app_data_offset) return true;

  AdvertDataParser parser(&packet->payload[app_data_offset], packet->payload_len - app_data_offset);
  if (!parser.isValid() || parser.getType() != _advert_type) return true;

  resetWindowIfNeeded();
  if (_saturated) return true;

  uint8_t seen_count = 0;
  uint8_t first_available = 0xFF;
  for (uint8_t i = 0; i < _max_per_period; i++) {
    if (filterContains(i, packet->payload)) {
      seen_count++;
    } else if (first_available == 0xFF) {
      first_available = i;
    }
  }

  if (seen_count >= _max_per_period) return false;
  if (first_available != 0xFF) addToFilter(first_available, packet->payload);
  return true;
}

bool AdvertRateLimitClassifier::handleBlockCommand(char* args, char* reply, size_t reply_len, BlockRuleFn block_rule, void* block_context) {
  return BasePacketFilterClassifier::handleBlockCommand(
    "advert",
    args,
    reply,
    reply_len,
    "Usage: block advert repeater 60 [max]",
    "OK - advert block added",
    block_rule,
    block_context
  );
}

bool AdvertRateLimitClassifier::parseAdvertType(const char* name, uint8_t* advert_type) {
  if (name == NULL || advert_type == NULL || name[0] == 0) return false;

  if (strcmp(name, "companion") == 0) {
    *advert_type = ADV_TYPE_CHAT;
  } else if (strcmp(name, "repeater") == 0) {
    *advert_type = ADV_TYPE_REPEATER;
  } else if (strcmp(name, "room") == 0) {
    *advert_type = ADV_TYPE_ROOM;
  } else {
    char* end = NULL;
    long value = strtol(name, &end, 10);
    if (end == name || *end != 0 || value <= 0 || value > 15) return false;
    *advert_type = (uint8_t)value;
  }

  return true;
}

const char* AdvertRateLimitClassifier::advertTypeName(uint8_t advert_type) {
  switch (advert_type) {
  case ADV_TYPE_CHAT: return "companion";
  case ADV_TYPE_REPEATER: return "repeater";
  case ADV_TYPE_ROOM: return "room";
  default: return "?";
  }
}

const char* AdvertRateLimitClassifier::formatAdvertType(uint8_t advert_type, char* dest, size_t dest_len) {
  const char* name = advertTypeName(advert_type);
  if (strcmp(name, "?") != 0) return name;
  snprintf(dest, dest_len, "%u", advert_type);
  return dest;
}
