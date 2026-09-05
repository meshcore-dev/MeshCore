#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pure per-broker packet-type allowlist helpers. MeshCore payload types occupy
// the low four bits of the packet header, so a uint16_t stores the complete
// 0..15 allowlist without dynamic allocation.
namespace MQTTPacketFilter {

static const uint8_t kMinPacketType = 0;
static const uint8_t kMaxPacketType = 15;
static const uint16_t kAllPacketTypes = 0xFFFFu;
// "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15" plus the terminator.
static const size_t kFilterTextSize = 38;

inline bool isAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline bool tokenEquals(const char* begin, size_t len, const char* token) {
  return token != nullptr && strlen(token) == len && memcmp(begin, token, len) == 0;
}

// Payload-type spellings accepted alongside the decimal form, mirroring the
// PAYLOAD_TYPE_* names in src/Packet.h. Types 12-14 are reserved upstream and
// have no name, so they stay reachable only by number.
struct NamedPacketType {
  const char* name;
  uint8_t type;
};

inline const NamedPacketType* namedPacketTypes(size_t* count_out) {
  static const NamedPacketType kNames[] = {
    {"req", 0},      {"response", 1}, {"txt_msg", 2},   {"ack", 3},
    {"advert", 4},   {"grp_txt", 5},  {"grp_data", 6},  {"anon_req", 7},
    {"path", 8},     {"trace", 9},    {"multipart", 10}, {"control", 11},
    {"raw_custom", 15},
  };
  if (count_out != nullptr) *count_out = sizeof(kNames) / sizeof(kNames[0]);
  return kNames;
}

// Resolve one already-trimmed list entry to a payload type. Decimal and named
// spellings are interchangeable within a list; "all"/"none" are whole-value
// keywords and deliberately do not resolve here, so "all,2" stays invalid.
inline bool resolveToken(const char* begin, size_t len, uint8_t* type_out) {
  if (begin == nullptr || type_out == nullptr || len == 0) return false;

  if (*begin >= '0' && *begin <= '9') {
    unsigned value = 0;
    for (size_t i = 0; i < len; ++i) {
      if (begin[i] < '0' || begin[i] > '9') return false;
      value = value * 10u + static_cast<unsigned>(begin[i] - '0');
      if (value > kMaxPacketType) return false;
    }
    *type_out = static_cast<uint8_t>(value);
    return true;
  }

  size_t name_count = 0;
  const NamedPacketType* names = namedPacketTypes(&name_count);
  for (size_t i = 0; i < name_count; ++i) {
    if (tokenEquals(begin, len, names[i].name)) {
      *type_out = names[i].type;
      return true;
    }
  }
  return false;
}

// Parse an allowlist value. Empty input means "all" so WebConfig can clear a
// field and retain the same backwards-compatible default as an older
// /mqtt_prefs file. Keywords and type names are deliberately lowercase; "all"
// and "none" cannot be mixed into a list. Entries may be decimal (0..15) or
// named, may carry surrounding ASCII whitespace, and may be repeated.
inline bool parse(const char* input, uint16_t* mask_out) {
  if (input == nullptr || mask_out == nullptr) return false;

  const char* begin = input;
  while (*begin && isAsciiSpace(*begin)) begin++;
  const char* end = begin + strlen(begin);
  while (end > begin && isAsciiSpace(end[-1])) end--;

  const size_t len = static_cast<size_t>(end - begin);
  if (len == 0 || tokenEquals(begin, len, "all")) {
    *mask_out = kAllPacketTypes;
    return true;
  }
  if (tokenEquals(begin, len, "none")) {
    *mask_out = 0;
    return true;
  }

  uint16_t parsed = 0;
  const char* cursor = begin;
  for (;;) {
    const char* comma = cursor;
    while (comma < end && *comma != ',') comma++;

    const char* token_begin = cursor;
    const char* token_end = comma;
    while (token_begin < token_end && isAsciiSpace(*token_begin)) token_begin++;
    while (token_end > token_begin && isAsciiSpace(token_end[-1])) token_end--;

    uint8_t type = 0;
    if (!resolveToken(token_begin, static_cast<size_t>(token_end - token_begin), &type)) {
      return false;
    }
    parsed |= static_cast<uint16_t>(1u << type);

    if (comma >= end) break;
    cursor = comma + 1;
  }

  *mask_out = parsed;
  return true;
}

// Format masks deterministically for CLI/API output. Subsets are emitted in
// ascending order; the two useful extremes use concise keywords.
inline bool format(uint16_t mask, char* output, size_t output_size) {
  if (output == nullptr || output_size == 0) return false;
  output[0] = '\0';

  const char* keyword = nullptr;
  if (mask == kAllPacketTypes) keyword = "all";
  else if (mask == 0) keyword = "none";
  if (keyword != nullptr) {
    const size_t len = strlen(keyword);
    if (output_size <= len) return false;
    memcpy(output, keyword, len + 1);
    return true;
  }

  char formatted[kFilterTextSize];
  size_t pos = 0;
  bool first = true;
  for (uint8_t type = kMinPacketType; type <= kMaxPacketType; ++type) {
    if ((mask & static_cast<uint16_t>(1u << type)) == 0) continue;
    if (!first) formatted[pos++] = ',';
    if (type >= 10) formatted[pos++] = '1';
    formatted[pos++] = static_cast<char>('0' + (type % 10));
    first = false;
  }
  formatted[pos] = '\0';

  if (output_size <= pos) return false;
  memcpy(output, formatted, pos + 1);
  return true;
}

// The keyword shortcut above means format() never actually emits the whole
// 0..15 list, but the emit loop is written for every type, so the internal
// buffer must still hold it: 22 digits + 15 separators + NUL.
static_assert(kFilterTextSize >= 38, "filter text buffer must hold the full 0..15 list");

inline bool allows(uint16_t mask, uint8_t packet_type) {
  return packet_type <= kMaxPacketType &&
      (mask & static_cast<uint16_t>(1u << packet_type)) != 0;
}

// How many types a mask allows. Lets a length-constrained reply fall back to
// "N/16" instead of a clipped list, which would read as a different allowlist.
inline uint8_t countTypes(uint16_t mask) {
  uint8_t total = 0;
  for (uint8_t type = kMinPacketType; type <= kMaxPacketType; ++type) {
    if ((mask & static_cast<uint16_t>(1u << type)) != 0) total++;
  }
  return total;
}

// Conservative "could any configured broker want this type?" mask, used to
// reject a packet before it is copied into the publish queue. Disabled slots
// contribute nothing; a slot whose topic style later turns out not to support
// the publication is still counted here, so the gate never drops a packet the
// per-slot pass would have published.
inline uint16_t enabledUnion(const uint16_t* masks, const bool* enabled, size_t count) {
  if (masks == nullptr) return 0;
  uint16_t combined = 0;
  for (size_t i = 0; i < count; ++i) {
    if (enabled != nullptr && !enabled[i]) continue;
    combined = static_cast<uint16_t>(combined | masks[i]);
  }
  return combined;
}

// True when every slot still carries the default all-types mask, i.e. nothing
// depends on the packet-filter tail of /mqtt_prefs being written.
inline bool allMasksDefault(const uint16_t* masks, size_t count) {
  if (masks == nullptr) return true;
  for (size_t i = 0; i < count; ++i) {
    if (masks[i] != kAllPacketTypes) return false;
  }
  return true;
}

// Stage one of the publish gate: everything decidable before a packet is
// serialised or a topic is built. Cheap enough to run per packet per slot.
inline bool slotCandidate(bool slot_enabled, uint16_t mask, uint8_t packet_type) {
  return slot_enabled && allows(mask, packet_type);
}

// The complete gate, once the slot's topic support is known. Eligibility
// deliberately excludes connection state: a temporarily disconnected broker
// that is configured for this type still requires the shared queue's existing
// bounded retry policy.
//
// eligiblePacketSlots() calls this directly, using slotCandidate() first only
// to skip the topic build for slots the mask already rejects. A slot that
// passes both therefore has its topic built again in the publish loop; that
// second build is the accepted cost of not carrying six 128-byte topics on the
// MQTT task stack.
inline bool slotEligible(bool slot_enabled, bool topic_supported,
                         uint16_t mask, uint8_t packet_type) {
  return slotCandidate(slot_enabled, mask, packet_type) && topic_supported;
}

// A fully filtered/topic-incompatible packet is intentionally complete. Once
// any eligible target exists, at least one actual publish must succeed.
inline bool publishComplete(bool has_eligible_target, bool any_publish_succeeded) {
  return any_publish_succeeded || !has_eligible_target;
}

}  // namespace MQTTPacketFilter
