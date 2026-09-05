#pragma once

#include <Packet.h>
#include <stddef.h>

// Sizing and validation for the scratch buffers that hold a serialized packet.
// Pure so the boundary conditions can be tested on the host: the firmware-side
// callers are MQTTMessageBuilder::packetToHex() and MQTTBridge::publishPacket().
namespace MQTTWireScratch {

// A serialized packet is header(1) + transport codes(0|4) + path_len(1) +
// path(<= MAX_PATH_SIZE) + payload(<= MAX_PACKET_PAYLOAD). Packet::writeTo()
// returns uint8_t, so MAX_TRANS_UNIT is the hard ceiling.
static const size_t kWireBytes = MAX_TRANS_UNIT;
// Two uppercase hex chars per byte, plus the NUL.
static const size_t kWireHexChars = 2 * MAX_TRANS_UNIT + 1;

static_assert(1 + 4 + 1 + MAX_PATH_SIZE + MAX_PACKET_PAYLOAD <= MAX_TRANS_UNIT,
              "serialized packet no longer fits MAX_TRANS_UNIT — resize the wire scratch buffers");

// True when Packet::writeTo() can safely serialize `packet` into `dest_size` bytes.
//
// writeTo() trusts the packet's own length fields and cannot report an overrun (its
// return type is uint8_t), so the source fields must be checked as well as the
// destination:
//  - payload_len drives an unchecked memcpy out of a MAX_PACKET_PAYLOAD array, and a
//    corrupt value can still leave getRawLength() inside MAX_TRANS_UNIT.
//  - path_len is written into a single wire byte, so anything above 255 is silently
//    truncated and would disagree with getPathByteLen().
//  - the path encoding must be one writePath() will actually emit. It self-guards
//    against overrunning the path array, but by writing nothing and returning 0, which
//    is a correctness problem rather than a safety one: getRawLength() still counts the
//    path, so an over-long or reserved encoding passes a destination-size check and
//    then serializes to a truncated frame that gets published as the packet. The worst
//    case is path_len 0xFF with no payload — 254 counted bytes, 2 bytes emitted.
//    isValidPathLen() rejects both the reserved 4-byte hash size and any
//    count * size above MAX_PATH_SIZE, and is the same predicate Packet::readFrom()
//    applies to every received packet, so no decodable packet is turned away.
inline bool canSerialize(const mesh::Packet& packet, size_t dest_size) {
  if (packet.payload_len > MAX_PACKET_PAYLOAD) return false;
  if (packet.path_len > 0xFF) return false;
  if (!mesh::Packet::isValidPathLen((uint8_t)packet.path_len)) return false;
  const int raw_len = packet.getRawLength();
  return raw_len > 0 && (size_t)raw_len <= dest_size;
}

}  // namespace MQTTWireScratch
