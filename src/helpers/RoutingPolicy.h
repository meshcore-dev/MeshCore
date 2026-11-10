#pragma once

#include <Packet.h>
#include <string.h>

namespace mesh {

// Number of payload type slots: the type is 4 bits (PH_TYPE_MASK), so 0..15
#define FLOOD_MAX_TYPE_SLOTS  16

// No entry for this payload type in the exception list, so no type specific limit applies
#define FLOOD_MAX_TYPE_NONE   0xFF

// Buffer size for the exception list, including the null. Worst case is all 16 slots set to 64:
// ten entries "0:64" to "9:64", six "10:64" to "15:64" and fifteen commas, so 85 characters plus the
// null (checked in test_routing_policy, BufferHoldsEverySlotAtItsLongest)
#define FLOOD_MAX_TYPES_LEN   88

/**
 * \brief  Read the per payload type exception list into a lookup table.
 * \param  list   "<type>:<hops>" entries separated by commas, eg. "6:4,9:3". NULL or "" means no exceptions.
 * \param  table  filled with the stored limit per payload type, FLOOD_MAX_TYPE_NONE where the list has no entry
 *
 * Parsing stops at the first malformed character, so a damaged list keeps the entries read so far and
 * loses the rest; it never applies a value it did not read. Entries out of range are skipped.
 */
inline void floodMaxTypesToTable(const char* list, uint8_t table[FLOOD_MAX_TYPE_SLOTS]) {
  for (int i = 0; i < FLOOD_MAX_TYPE_SLOTS; i++) table[i] = FLOOD_MAX_TYPE_NONE;
  if (list == NULL) return;
  while (*list) {
    uint16_t type = 0, hops = 0;
    bool have_type = false, have_hops = false;
    while (*list >= '0' && *list <= '9' && type < 1000) { type = type * 10 + (*list++ - '0'); have_type = true; }
    if (!have_type || *list != ':') return;
    list++;
    while (*list >= '0' && *list <= '9' && hops < 1000) { hops = hops * 10 + (*list++ - '0'); have_hops = true; }
    if (!have_hops) return;
    if (type < FLOOD_MAX_TYPE_SLOTS && hops <= 64) table[type] = (uint8_t) hops;
    if (*list != ',') return;
    list++;
  }
}

/**
 * \brief  The limit the exception list stores for one payload type.
 * \returns  the stored hop limit, or FLOOD_MAX_TYPE_NONE when the list holds no entry for this type
 */
inline uint8_t floodMaxForType(const char* list, uint8_t payload_type) {
  if (list == NULL || *list == 0 || payload_type >= FLOOD_MAX_TYPE_SLOTS) return FLOOD_MAX_TYPE_NONE;
  uint8_t table[FLOOD_MAX_TYPE_SLOTS];
  floodMaxTypesToTable(list, table);
  return table[payload_type];
}

/**
 * \brief  Write a lookup table back out as an exception list, sorted by payload type, no duplicates.
 * \param  cap  size of out, including the null
 * \returns  false if the result does not fit in cap; out is then left untouched
 */
inline bool floodMaxTypesFromTable(const uint8_t table[FLOOD_MAX_TYPE_SLOTS], char* out, size_t cap) {
  char buf[FLOOD_MAX_TYPES_LEN];
  size_t n = 0;
  for (uint8_t type = 0; type < FLOOD_MAX_TYPE_SLOTS; type++) {
    if (table[type] == FLOOD_MAX_TYPE_NONE) continue;
    if (n > 0) { if (n + 1 >= sizeof(buf)) return false; buf[n++] = ','; }
    uint8_t vals[2] = { type, table[type] };
    for (int v = 0; v < 2; v++) {
      if (v == 1) { if (n + 1 >= sizeof(buf)) return false; buf[n++] = ':'; }
      if (vals[v] >= 10) { if (n + 1 >= sizeof(buf)) return false; buf[n++] = '0' + vals[v] / 10; }
      if (n + 1 >= sizeof(buf)) return false;
      buf[n++] = '0' + vals[v] % 10;
    }
  }
  if (n + 1 > cap) return false;
  memcpy(out, buf, n);
  out[n] = 0;
  return true;
}

/**
 * \brief  Insert, change or remove one payload type's entry in the exception list, in place.
 * \param  list  the list to edit; stays sorted by payload type, with no duplicates
 * \param  cap   size of the list buffer, including the null
 * \param  hops  the limit to store (0..64), or FLOOD_MAX_TYPE_NONE to remove the entry
 * \returns  false if the payload type or the limit is out of range, or the result would not fit;
 *           the list is then left unchanged
 */
inline bool setFloodMaxForType(char* list, size_t cap, uint8_t payload_type, uint8_t hops) {
  if (payload_type >= FLOOD_MAX_TYPE_SLOTS) return false;
  if (hops > 64 && hops != FLOOD_MAX_TYPE_NONE) return false;
  uint8_t table[FLOOD_MAX_TYPE_SLOTS];
  floodMaxTypesToTable(list, table);
  table[payload_type] = hops;
  return floodMaxTypesFromTable(table, list, cap);
}

// Error codes of parseFloodMaxTypeCmd
#define FLOOD_MAX_CMD_OK            0
#define FLOOD_MAX_CMD_BAD_TYPE    (-1)
#define FLOOD_MAX_CMD_BAD_HOPS    (-2)

/**
 * \brief  The name of a payload type as "flood.max.type" accepts and prints it.
 * \returns  the name, or NULL for the type numbers that are not assigned (0x0C, 0x0D, 0x0E)
 */
inline const char* floodMaxTypeName(uint8_t payload_type) {
  static const char* const names[FLOOD_MAX_TYPE_SLOTS] = {
    "req", "response", "txt", "ack", "advert", "grptxt", "grpdata", "anonreq",
    "path", "trace", "multipart", "control", NULL, NULL, NULL, "raw"
  };
  return payload_type < FLOOD_MAX_TYPE_SLOTS ? names[payload_type] : NULL;
}

/**
 * \brief  The payload type a word names, by name or as a plain number 0..15.
 * \returns  the payload type, or -1 if the word is neither
 *
 * A plain number always works, so a payload type added later can be given a hop limit without a
 * firmware change, even before it has a name here.
 */
inline int floodMaxTypeByName(const char* word) {
  if (word[0] == 0) return -1;
  if (word[0] >= '0' && word[0] <= '9') {
    int n = 0;
    for (const char* d = word; *d; d++) {
      if (*d < '0' || *d > '9') return -1;
      n = n * 10 + (*d - '0');
      if (n >= FLOOD_MAX_TYPE_SLOTS) return -1;
    }
    return n;
  }
  for (uint8_t i = 0; i < FLOOD_MAX_TYPE_SLOTS; i++) {
    const char* name = floodMaxTypeName(i);
    if (name != NULL && strcmp(name, word) == 0) return i;
  }
  return -1;
}

/**
 * \brief  Read the arguments of "set flood.max.type <type> <hops|off>".
 * \param  args  everything after the command, eg. "grpdata 4" or "trace off"
 * \param  payload_type  set to the type named
 * \param  hops  set to the limit, or FLOOD_MAX_TYPE_NONE for "off"
 * \returns  FLOOD_MAX_CMD_OK, FLOOD_MAX_CMD_BAD_TYPE or FLOOD_MAX_CMD_BAD_HOPS
 */
inline int parseFloodMaxTypeCmd(const char* args, uint8_t* payload_type, uint8_t* hops) {
  while (*args == ' ') args++;
  char word[16];
  size_t n = 0;
  while (*args && *args != ' ' && n < sizeof(word) - 1) word[n++] = *args++;
  word[n] = 0;
  while (*args && *args != ' ') args++;   // a word longer than the buffer is not a type either
  while (*args == ' ') args++;

  int type = floodMaxTypeByName(word);
  if (type < 0) return FLOOD_MAX_CMD_BAD_TYPE;

  if (strcmp(args, "off") == 0) {
    *payload_type = (uint8_t) type;
    *hops = FLOOD_MAX_TYPE_NONE;
    return FLOOD_MAX_CMD_OK;
  }
  if (args[0] < '0' || args[0] > '9') return FLOOD_MAX_CMD_BAD_HOPS;
  int m = 0;
  for (const char* d = args; *d; d++) {
    if (*d < '0' || *d > '9') return FLOOD_MAX_CMD_BAD_HOPS;
    m = m * 10 + (*d - '0');
    if (m > 64) return FLOOD_MAX_CMD_BAD_HOPS;
  }
  *payload_type = (uint8_t) type;
  *hops = (uint8_t) m;
  return FLOOD_MAX_CMD_OK;
}

/**
 * \brief  Render the exception list as "get flood.max.type" answers it, eg. "> grpdata:4 trace:3".
 * \param  reply  filled with the answer, including the leading "> "; "> none" for an empty list
 * \param  cap    size of reply, including the null
 *
 * A list too long for the buffer ends in " ...", so the answer never runs past cap.
 */
inline void formatFloodMaxTypes(const char* list, char* reply, size_t cap) {
  uint8_t table[FLOOD_MAX_TYPE_SLOTS];
  floodMaxTypesToTable(list, table);
  size_t n = 0;
  if (cap >= 3) { reply[n++] = '>'; reply[n++] = ' '; }
  bool any = false;
  for (uint8_t type = 0; type < FLOOD_MAX_TYPE_SLOTS; type++) {
    if (table[type] == FLOOD_MAX_TYPE_NONE) continue;
    char entry[24];
    size_t e = 0;
    if (any) entry[e++] = ' ';
    const char* name = floodMaxTypeName(type);
    if (name != NULL) {
      while (*name) entry[e++] = *name++;
    } else {
      if (type >= 10) entry[e++] = '0' + type / 10;
      entry[e++] = '0' + type % 10;
    }
    entry[e++] = ':';
    if (table[type] >= 10) entry[e++] = '0' + table[type] / 10;
    entry[e++] = '0' + table[type] % 10;
    entry[e] = 0;
    if (n + e + 1 > cap) {           // no room: say so instead of cutting an entry in half
      const char* more = " ...";
      while (*more && n + 1 < cap) reply[n++] = *more++;
      break;
    }
    memcpy(reply + n, entry, e);
    n += e;
    any = true;
  }
  if (!any && n + 5 <= cap) { memcpy(reply + n, "none", 4); n += 4; }
  reply[n] = 0;
}

/**
 * \brief  Test a flood packet against the configured hop limits.
 * \param  packet  inbound flood packet (caller has already checked isRouteFlood())
 * \param  flood_max            max hops for any flood packet
 * \param  flood_max_unscoped   max hops for ROUTE_TYPE_FLOOD (ie. un-scoped) packets
 * \param  flood_max_advert     max hops for ADVERT packets
 * \param  flood_max_types      per payload type exception list, or NULL for none
 * \returns  true if the packet has exceeded a limit, and must not be forwarded
 *
 * Every limit that applies is tested, so the strictest one wins: that is how flood_max and
 * flood_max_unscoped have always worked together, and the exception list joins them on the same terms.
 * With an empty (or NULL) list the result is bit for bit what it was before the list existed.
 */
inline bool isFloodHopLimitExceeded(const Packet* packet, uint8_t flood_max,
                                    uint8_t flood_max_unscoped, uint8_t flood_max_advert,
                                    const char* flood_max_types = NULL) {
  uint8_t hops = packet->getPathHashCount();
  if (hops >= flood_max) return true;
  if (packet->getRouteType() == ROUTE_TYPE_FLOOD && hops >= flood_max_unscoped) return true;
  if (packet->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD && packet->transport_codes[0] == 0 && hops >= flood_max_unscoped) return true;  // scope 0, treat as unscoped
  if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && hops >= flood_max_advert) return true;
  uint8_t per_type = floodMaxForType(flood_max_types, packet->getPayloadType());
  if (per_type != FLOOD_MAX_TYPE_NONE && hops >= per_type) return true;
  return false;
}

/**
 * \brief  How a server routes a reply back to the requesting client.
 */
enum ReplyRoute : uint8_t {
  REPLY_ROUTE_PATH_RETURN,       // request arrived by flood: reply with a PATH return, flooded back
  REPLY_ROUTE_DIRECT_SUPPLIED,   // reply DIRECT, along the return path supplied in the request
  REPLY_ROUTE_DIRECT_OUT_PATH,   // reply DIRECT, along the out_path already stored for this client
  REPLY_ROUTE_FLOOD,             // no return path known: flood the reply
};

/**
 * \param  inbound_is_flood    the request arrived as a flood packet
 * \param  have_supplied_path  the request payload carried an explicit reply path
 * \param  have_out_path       this server already has a stored out_path for the client
 */
inline ReplyRoute chooseReplyRoute(bool inbound_is_flood, bool have_supplied_path, bool have_out_path) {
  if (inbound_is_flood) return REPLY_ROUTE_PATH_RETURN;
  if (have_supplied_path) return REPLY_ROUTE_DIRECT_SUPPLIED;
  if (have_out_path) return REPLY_ROUTE_DIRECT_OUT_PATH;
  return REPLY_ROUTE_FLOOD;
}

/**
 * \brief  Which transport scope a flooded reply should be sent with.
 */
enum ReplyScope : uint8_t {
  REPLY_SCOPE_REQUEST,   // re-use the scope the request arrived on
  REPLY_SCOPE_DEFAULT,   // fall back to this node's default region scope
  REPLY_SCOPE_NONE,      // send un-scoped (ROUTE_TYPE_FLOOD)
};

/**
 * \param  request_scope_known         request arrived scoped, and we resolved its Region's key
 * \param  request_was_unscoped_flood  request arrived as an un-scoped flood
 * \param  default_scope_known         this node has a default Region with a usable transport key
 */
inline ReplyScope chooseReplyScope(bool request_scope_known, bool request_was_unscoped_flood,
                                   bool default_scope_known) {
  if (request_scope_known) return REPLY_SCOPE_REQUEST;
  if (request_was_unscoped_flood) return REPLY_SCOPE_NONE;  // requester chose un-scoped, so mirror it
  if (default_scope_known) return REPLY_SCOPE_DEFAULT;      // scope unknowable: DIRECT, or unresolved Region
  return REPLY_SCOPE_NONE;
}

}
