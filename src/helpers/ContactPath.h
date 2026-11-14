#pragma once

#include <MeshCore.h> // MAX_PATH_SIZE
#include <stdint.h>
#include <string.h>

// Bytes reserved for a contact's stored direct route. Boards with very little RAM can trade long direct
// routes for contact table space: a route that does not fit is not stored (OUT_PATH_UNKNOWN), and messages to
// that contact go out flood instead. The default keeps the full MAX_PATH_SIZE.
#ifndef CONTACT_OUT_PATH_SIZE
#define CONTACT_OUT_PATH_SIZE MAX_PATH_SIZE
#endif

#ifndef OUT_PATH_UNKNOWN
#define OUT_PATH_UNKNOWN 0xFF
#endif

// Number of bytes of an encoded path_len (upper 2 bits: hash size - 1, lower 6 bits: hop count),
// or 0xFF for the reserved hash size 4.
inline uint8_t contactPathBytes(uint8_t path_len) {
  uint8_t hash_size = (path_len >> 6) + 1;
  if (hash_size == 4) return 0xFF;
  return (uint8_t)((path_len & 63) * hash_size);
}

// Stores a route into a contact's out_path. Returns the stored path_len, or OUT_PATH_UNKNOWN when it does not
// fit.
inline uint8_t contactStorePath(uint8_t *dest, const uint8_t *src, uint8_t path_len) {
  if (path_len == OUT_PATH_UNKNOWN) return OUT_PATH_UNKNOWN;
  uint8_t n = contactPathBytes(path_len);
  if (n > CONTACT_OUT_PATH_SIZE || n > MAX_PATH_SIZE) return OUT_PATH_UNKNOWN;
  memcpy(dest, src, n);
  return path_len;
}

// Protocol frames and files always carry MAX_PATH_SIZE path bytes: copies the stored route and zeroes the
// rest.
inline void contactWritePath64(uint8_t *dest64, const uint8_t *stored, uint8_t path_len) {
  memset(dest64, 0, MAX_PATH_SIZE);
  if (path_len == OUT_PATH_UNKNOWN) return;
  uint8_t n = contactPathBytes(path_len);
  if (n > CONTACT_OUT_PATH_SIZE) return; // never stored that way: nothing valid to copy
  memcpy(dest64, stored, n);
}

// Call-site helpers. With the default CONTACT_OUT_PATH_SIZE they compile to exactly the operation each call
// site used before (so other boards are unchanged); with a smaller size they clamp as above.
#if CONTACT_OUT_PATH_SIZE < MAX_PATH_SIZE
inline uint8_t contactPathFromPacket(uint8_t *dest, const uint8_t *src, uint8_t path_len) {
  return contactStorePath(dest, src, path_len);
}
inline uint8_t contactPathFrom64(uint8_t *dest, const uint8_t *src64, uint8_t path_len) {
  return contactStorePath(dest, src64, path_len);
}
inline void contactPathTo64(uint8_t *dest64, const uint8_t *stored, uint8_t path_len) {
  contactWritePath64(dest64, stored, path_len);
}
#else
#include <Packet.h>
inline uint8_t contactPathFromPacket(uint8_t *dest, const uint8_t *src, uint8_t path_len) {
  return mesh::Packet::copyPath(dest, src, path_len);
}
inline uint8_t contactPathFrom64(uint8_t *dest, const uint8_t *src64, uint8_t path_len) {
  memcpy(dest, src64, MAX_PATH_SIZE);
  return path_len;
}
inline void contactPathTo64(uint8_t *dest64, const uint8_t *stored, uint8_t path_len) {
  (void)path_len;
  memcpy(dest64, stored, MAX_PATH_SIZE);
}
#endif
