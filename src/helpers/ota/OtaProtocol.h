#pragma once

#include <stdint.h>
#include <stddef.h>
#include "OtaFormat.h"

// Encode/decode for the OTA LoRa messages (docs/ota_protocol.md §8). Each message is a packet payload:
// [0]=ota_msg_type, then a fixed body. Portable + allocation-free; unit-tested on the host.
//
// manifest_id == the manifest's merkle_root (4 bytes), a compact content id.

namespace mesh {
namespace ota {

// ---- OTA_ADV: tiny per-NODE beacon (flood, periodic). CONSTANT size regardless of how many mOTAs a
// node serves — it just says "I'm a source, here's how many + a digest of my set". A peer that's
// interested asks for the catalog via OTA_QUERY. (Replaces the old per-mOTA advert so a folder node with
// N images costs one 10-byte beacon, not N adverts.) ----
struct AdvMsg {
  uint8_t  seeder_id[4];     // advertiser's node id = pubkey[0:4]; the QUERY address + distinct-source id
  uint8_t  n_motas;          // # of complete, servable mOTAs (saturates at 255)
  uint8_t  set_digest[4];    // sha2-256:4 over the sorted set of served mids; "did my offering change?"
};

// ---- OTA_QUERY: "list what you serve" — addressed to a source by seeder_id, FLOODED so neighbours
// overhear it (storm suppression). set_digest identifies the offering being asked about (so an overhearer
// can suppress its own pending query for the same {source,digest}). filter_target=0 = everything. ----
struct QueryMsg {
  uint8_t  seeder_id[4];     // which source this query is for (the source matches its own id)
  uint8_t  set_digest[4];    // the offering digest we're asking about (for overhear-suppression)
  uint32_t filter_target;    // 0 = all (the scalable default); else only mOTAs for this target_id
};

// ---- OTA_HAVE: the compact catalog (source -> mesh), FLOODED + tagged with set_digest so EVERY node
// that overhears it caches the rows (passive, no query needed). Fragmented. ----
// body: seeder_id(4) set_digest(4) frag_idx(1) frag_total(1) n_rows(1) rows[ mid(4) target(4) fwver(4) codec(1) flags(1) ]
struct HaveRow { uint8_t mid[4]; uint32_t target_id; uint32_t fw_version; uint8_t codec_id; uint8_t flags;
                 uint16_t have_count; };   // blocks the advertiser holds (== block_count if complete; less => partial source)
struct HaveMsg {
  uint8_t  seeder_id[4];
  uint8_t  set_digest[4];    // the offering this catalog describes (overhearers cache by it)
  uint8_t  frag_idx, frag_total;
  uint8_t  n_rows;           // rows in THIS fragment
  const uint8_t* rows;       // points into buf: n_rows * OTA_HAVE_ROW_BYTES
};
static const uint8_t OTA_HAVE_ROW_BYTES = 16;   // mid4 + target4 + fwver4 + codec1 + flags1 + have_count2

// ---- OTA_GET_MANIFEST: request the manifest for a content id (direct) ----
struct GetManifestMsg { uint8_t manifest_id[4]; };

// ---- OTA_MANIFEST: the manifest-minus-leaves[], fragmented (direct) ----
// body: manifest_id(4) frag_idx(1) frag_total(1) bytes[]
struct ManifestMsg {
  uint8_t  manifest_id[4];
  uint8_t  frag_idx, frag_total;
  const uint8_t* bytes; uint16_t len;
};

// ---- OTA_REQ: request a window of blocks (direct) ----
struct ReqMsg { uint8_t manifest_id[4]; uint16_t start_block; uint8_t count; };

// ---- OTA_DATA: one self-describing fragment of a block's data (proof is fetched separately) ----
// body: manifest_id(4) block_idx(2) frag_off(2) data[]
// `frag_off` is the byte offset of `data` within block `block_idx` (global position = block_idx*block_size
// + frag_off), so a fragment is self-placing and can be requested from ANY peer (BitTorrent-style).
struct DataMsg {
  uint8_t  manifest_id[4];
  uint16_t block_idx;
  uint16_t frag_off;
  const uint8_t* data; uint16_t data_len;
};

// ---- OTA_REQ_PROOF: request the merkle proof for one (reassembled) block (direct) ----
struct ReqProofMsg { uint8_t manifest_id[4]; uint16_t block_idx; };

// ---- OTA_PROOF: the merkle proof (ordered sibling digests) for one block (direct) ----
struct ProofMsg { uint8_t manifest_id[4]; uint16_t block_idx; uint8_t n_proof; const uint8_t* proof; };

// Each encode_* returns the total payload length (incl. the leading msg-type byte), 0 on overflow.
// Each decode_* returns true on success (and points struct fields into `buf`).

uint16_t encode_adv(uint8_t* buf, uint16_t cap, const AdvMsg& m);
bool     decode_adv(const uint8_t* buf, uint16_t len, AdvMsg& m);

uint16_t encode_query(uint8_t* buf, uint16_t cap, const QueryMsg& m);
bool     decode_query(const uint8_t* buf, uint16_t len, QueryMsg& m);

uint16_t encode_have(uint8_t* buf, uint16_t cap, const HaveMsg& m);
bool     decode_have(const uint8_t* buf, uint16_t len, HaveMsg& m);

uint16_t encode_get_manifest(uint8_t* buf, uint16_t cap, const GetManifestMsg& m);
bool     decode_get_manifest(const uint8_t* buf, uint16_t len, GetManifestMsg& m);

uint16_t encode_manifest(uint8_t* buf, uint16_t cap, const ManifestMsg& m);
bool     decode_manifest(const uint8_t* buf, uint16_t len, ManifestMsg& m);

uint16_t encode_req(uint8_t* buf, uint16_t cap, const ReqMsg& m);
bool     decode_req(const uint8_t* buf, uint16_t len, ReqMsg& m);

uint16_t encode_data(uint8_t* buf, uint16_t cap, const DataMsg& m);
bool     decode_data(const uint8_t* buf, uint16_t len, DataMsg& m);

uint16_t encode_req_proof(uint8_t* buf, uint16_t cap, const ReqProofMsg& m);
bool     decode_req_proof(const uint8_t* buf, uint16_t len, ReqProofMsg& m);

uint16_t encode_proof(uint8_t* buf, uint16_t cap, const ProofMsg& m);
bool     decode_proof(const uint8_t* buf, uint16_t len, ProofMsg& m);

inline uint8_t ota_msg_type(const uint8_t* buf, uint16_t len) { return len ? buf[0] : 0xFF; }

} // namespace ota
} // namespace mesh
