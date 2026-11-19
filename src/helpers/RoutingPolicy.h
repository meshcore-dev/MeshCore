#pragma once

#include <Packet.h>

namespace mesh {

/**
 * \brief  Test a flood packet against the configured hop limits.
 * \param  packet  inbound flood packet (caller has already checked isRouteFlood())
 * \param  flood_max            max hops for any flood packet
 * \param  flood_max_unscoped   max hops for ROUTE_TYPE_FLOOD (ie. un-scoped) packets
 * \param  flood_max_advert     max hops for ADVERT packets
 * \returns  true if the packet has exceeded a limit, and must not be forwarded
 */
inline bool isFloodHopLimitExceeded(const Packet* packet, uint8_t flood_max,
                                    uint8_t flood_max_unscoped, uint8_t flood_max_advert) {
  uint8_t hops = packet->getPathHashCount();
  if (hops >= flood_max) return true;
  if (packet->getRouteType() == ROUTE_TYPE_FLOOD && hops >= flood_max_unscoped) return true;
  if (packet->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD && packet->transport_codes[0] == 0 && hops >= flood_max_unscoped) return true;  // scope 0, treat as unscoped
  if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && hops >= flood_max_advert) return true;
  return false;
}

/**
 * \brief  Can one more hash be appended to a flood packet's path?
 *
 * path_len packs the hash count in bits 0-5 (so at most 63) and the hash size - 1 in bits 6-7.
 * Checking only the byte limit ((n + 1) * size <= MAX_PATH_SIZE) lets a 64th 1-byte hash through:
 * setPathHashCount(64) then spills into bit 6, and the packet goes back on air with a hash count
 * of 0 and a hash size of 2, ie. as a brand-new zero-hop flood with a fresh hop budget.
 * \param  packet  inbound flood packet
 * \returns  true if both the byte limit and the 6-bit count limit hold after the append
 */
inline bool canAppendPathHash(const Packet* packet) {
  uint8_t n = packet->getPathHashCount();
  uint8_t sz = packet->getPathHashSize();
  if (sz > 3) return false;  // reserved hash size
  return n < 63 && (n + 1) * sz <= MAX_PATH_SIZE;
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
