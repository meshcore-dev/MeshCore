#include "TrustedClockTable.h"

static void copy_name(char* dest, const char* src, size_t cap) {
  if (cap == 0) return;
  if (src == NULL) { dest[0] = 0; return; }
  size_t i = 0;
  for (; i + 1 < cap && src[i] != 0; i++) dest[i] = src[i];
  dest[i] = 0;
}

TrustedClockSource* TrustedClockTable::find(const uint8_t* pub_key) {
  for (uint8_t i = 0; i < _count; i++) {
    if (memcmp(_sources[i].pub_key, pub_key, PUB_KEY_SIZE) == 0) return &_sources[i];
  }
  return NULL;
}

TrustedClockSource* TrustedClockTable::findByName(const char* name) {
  if (name == NULL || name[0] == 0) return NULL;
  for (uint8_t i = 0; i < _count; i++) {
    if (strcmp(_sources[i].name, name) == 0) return &_sources[i];
  }
  return NULL;
}

bool TrustedClockTable::add(const uint8_t* pub_key, const char* name) {
  TrustedClockSource* existing = find(pub_key);
  if (existing != NULL) {
    if (name != NULL) copy_name(existing->name, name, sizeof(existing->name));
    return true;
  }
  if (_count >= MAX_TRUSTED_CLOCK_KEYS) return false;
  TrustedClockSource* slot = &_sources[_count++];
  memcpy(slot->pub_key, pub_key, PUB_KEY_SIZE);
  slot->last_timestamp = 0;
  copy_name(slot->name, name, sizeof(slot->name));
  return true;
}

bool TrustedClockTable::remove(const uint8_t* pub_key) {
  for (uint8_t i = 0; i < _count; i++) {
    if (memcmp(_sources[i].pub_key, pub_key, PUB_KEY_SIZE) == 0) {
      for (uint8_t j = i + 1; j < _count; j++) _sources[j - 1] = _sources[j];
      _count--;
      memset(&_sources[_count], 0, sizeof(_sources[_count]));
      return true;
    }
  }
  return false;
}

const ClockSyncEvent& TrustedClockTable::syncEventNewestFirst(uint8_t idx_from_newest) const {
  // Caller must ensure idx_from_newest < _num_events
  int idx = ((int)_next_event_idx - 1 - (int)idx_from_newest + MAX_SYNC_EVENTS) % MAX_SYNC_EVENTS;
  return _events[idx];
}

void TrustedClockTable::recordSyncEvent(const uint8_t* pub_key, const char* name,
                                        uint32_t local_before, uint32_t advert_timestamp) {
  ClockSyncEvent& slot = _events[_next_event_idx];
  memcpy(slot.pub_key_prefix, pub_key, sizeof(slot.pub_key_prefix));
  copy_name(slot.name, name, sizeof(slot.name));
  slot.local_before = local_before;
  slot.advert_timestamp = advert_timestamp;
  _next_event_idx = (_next_event_idx + 1) % MAX_SYNC_EVENTS;
  if (_num_events < MAX_SYNC_EVENTS) _num_events++;
}

size_t TrustedClockTable::serialize(uint8_t* buf, size_t cap) const {
  size_t need = serializedSize();
  if (cap < need) return 0;
  buf[0] = FORMAT_VERSION;
  buf[1] = _count;
  size_t off = HEADER_SIZE;
  for (uint8_t i = 0; i < _count; i++) {
    memcpy(buf + off, _sources[i].pub_key, PUB_KEY_SIZE);
    off += PUB_KEY_SIZE;
    memcpy(buf + off, _sources[i].name, TRUSTED_CLOCK_NAME_LEN);
    off += TRUSTED_CLOCK_NAME_LEN;
  }
  return off;
}

bool TrustedClockTable::deserialize(const uint8_t* buf, size_t len) {
  _count = 0;
  if (len < HEADER_SIZE) return false;
  if (buf[0] != FORMAT_VERSION) return false;
  uint8_t want = buf[1];
  if (want > MAX_TRUSTED_CLOCK_KEYS) want = MAX_TRUSTED_CLOCK_KEYS;
  size_t off = HEADER_SIZE;
  for (uint8_t i = 0; i < want; i++) {
    if (off + RECORD_SIZE > len) break;   // truncated; stop
    memcpy(_sources[i].pub_key, buf + off, PUB_KEY_SIZE);
    off += PUB_KEY_SIZE;
    memcpy(_sources[i].name, buf + off, TRUSTED_CLOCK_NAME_LEN);
    off += TRUSTED_CLOCK_NAME_LEN;
    _sources[i].name[TRUSTED_CLOCK_NAME_LEN - 1] = 0;
    _sources[i].last_timestamp = 0;
    _count++;
  }
  return true;
}
