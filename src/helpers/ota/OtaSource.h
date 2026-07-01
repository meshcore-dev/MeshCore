#pragma once

#include <stdint.h>
#include <stddef.h>
#include "OtaFormat.h"

// Transport-agnostic "folder of firmware" abstraction (docs/ota_protocol.md §9). A node can RELAY
// `.mota` images it does not hold in flash — a user drops several `.mota` (different architectures) into
// some external store and the node advertises + serves them as if it held them. Peers just see "this node
// has N mOTAs"; the node knows they are external. The store is reached through a MotaSource, so the SAME
// serve code drives a USB-serial host daemon, BLE, a WiFi URL list, an NFS/samba mount, ... — only the
// `read()` plumbing differs per transport.
//
// The relay is TRUSTLESS: the fetcher verifies every block against the signed merkle root, so a source is
// never trusted. A wrong descriptor or wrong bytes simply makes the fetch fail its merkle/signature check
// — a malicious or buggy source cannot forge firmware, only deny it.

namespace mesh {
namespace ota {

// A parsed top-level descriptor of one `.mota` a source provides: enough to advertise it in the catalog
// AND to locate every region for serving, WITHOUT holding the whole image in RAM. Offsets are absolute
// byte positions within the `.mota` container (which always begins MAGIC(4) total(4) manifest...).
struct MotaDesc {
  uint8_t  mid[4] = {0};        // merkle_root (the content id peers fetch by)
  uint32_t target_id = 0;
  uint32_t fw_version = 0;
  uint8_t  codec_id = 0;
  uint8_t  flags = 0;
  uint32_t total_size = 0;      // full `.mota` length (bytes)
  uint32_t leaves_off = 0;      // byte offset of the merkle leaves[] (manifest-minus-leaves = [8, leaves_off))
  uint32_t block_count = 0;     // == number of leaves (== number of payload blocks)
  uint32_t payload_off = 0;     // byte offset of the payload
  uint32_t payload_size = 0;
};

// One or more complete `.mota` images, reachable as random-access bytes. Implementations are device-side
// and transport-specific (the engine in OtaManager is portable and never includes this directly for I/O;
// it only calls through the interface).
class MotaSource {
public:
  virtual ~MotaSource() {}
  // Number of complete, servable mOTAs this source currently offers (may change as the folder changes;
  // the manager re-enumerates on add_source / refresh).
  virtual uint8_t count() = 0;
  // Cheap metadata + region offsets for mota `idx`. False if idx is out of range or unparsable.
  virtual bool    describe(uint8_t idx, MotaDesc& out) = 0;
  // Random-access read of `len` bytes at absolute offset `off` of mota `idx` into `buf`. Returns true iff
  // exactly `len` bytes were produced. May block on the transport (serial round-trip); OTA is lowest
  // priority so latency here is acceptable.
  virtual bool    read(uint8_t idx, uint32_t off, uint8_t* buf, uint32_t len) = 0;
};

} // namespace ota
} // namespace mesh
