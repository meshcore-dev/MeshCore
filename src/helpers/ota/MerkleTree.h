#pragma once

#include <stdint.h>
#include <stddef.h>

// Merkle tree over PAYLOAD blocks, sha2-256:4 (4-byte) leaves/nodes. See docs/ota_protocol.md §6.
//
// Scheme: leaf = H(block); node = H(left || right); on an odd level the last node is promoted
// unchanged (no duplication). Root = single remaining node.
//
// No dynamic allocation: the root is computed with an O(log count) "binary counter" of partial
// peaks (<= 32 levels => 128 bytes of stack). Proofs carry only sibling digests; the left/right
// direction is derived from the block index + total count (no direction bits on the wire).

namespace mesh {
namespace ota {

// leaf digest of one payload block
void merkle_leaf(uint8_t out[4], const uint8_t* block, uint32_t block_len);

// parent of two 4-byte children
void merkle_combine(uint8_t out[4], const uint8_t* left, const uint8_t* right);

// root over `count` contiguous 4-byte leaf digests (leaves[count*4]). count >= 1.
void merkle_root(uint8_t out[4], const uint8_t* leaves, uint32_t count);

// Verify that `block` is block `index` of a `count`-block payload whose tree has the given `root`.
// `siblings` is n_siblings contiguous 4-byte digests, ordered leaf->root (promoted levels omitted;
// left/right direction derived from index + count).
bool merkle_verify(const uint8_t* block, uint32_t block_len, uint32_t index,
                   const uint8_t* siblings, uint8_t n_siblings,
                   const uint8_t root[4], uint32_t count);

// Same, but starting from a precomputed 4-byte leaf digest (skips the H(block) step).
bool merkle_verify_from_leaf(const uint8_t leaf[4], uint32_t index,
                             const uint8_t* siblings, uint8_t n_siblings,
                             const uint8_t root[4], uint32_t count);

// Generate the proof (ordered sibling digests) for block `index`, for a server holding leaves[].
// `scratch` must be >= count*4 bytes (working buffer); `out_siblings` >= 32*4 bytes.
// Returns the number of 4-byte siblings written. Output matches the wire form merkle_verify expects.
uint8_t merkle_gen_proof(const uint8_t* leaves, uint32_t count, uint32_t index,
                         uint8_t* scratch, uint8_t* out_siblings);

} // namespace ota
} // namespace mesh
