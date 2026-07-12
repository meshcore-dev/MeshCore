#pragma once

#include <Mesh.h>
#include <stddef.h>
#include <stdint.h>

// Wire marker and signature domain are deliberately separate: the marker
// identifies the binary payload, while the domain prevents cross-protocol
// signature reuse.
#define TIME_SYNC_BINARY_MARKER "Tv1"
#define TIME_SYNC_CANONICAL_DOMAIN "MeshCore-Time-v1"

// Group datagram application type reserved in docs/number_allocations.md.
#define TIME_SYNC_BINARY_DATA_TYPE 0x0121

// Clock policy bounds keep malformed or far-future authenticated packets from
// placing unattended repeaters outside the firmware-supported Unix range.
#define TIME_SYNC_MIN_UNIX_TIME 1715770351UL
#define TIME_SYNC_MAX_UNIX_TIME 2147483647UL
#define TIME_SYNC_DEFAULT_MAX_FORWARD_STEP 3600UL

// Display names are included in the signed bytes and on the binary wire. The
// limit keeps the datagram comfortably below the practical group payload size.
#define TIME_SYNC_MAX_DISPLAY_NAME_LEN 20
#define TIME_SYNC_CANONICAL_MAX_LEN 128

// Lightweight view over persisted repeater settings. It avoids copying the
// pinned public key into packet-processing code.
struct TimeSyncConfigView {
  bool enabled;
  mesh::GroupChannel channel;
  const char* display_name;
  const uint8_t* public_key;
};

// Authenticated fields returned to the repeater clock policy after signature
// verification succeeds.
struct TimeSyncMessage {
  uint32_t timestamp;
  uint16_t sequence;
};

// Parser outcomes are intentionally coarse so the repeater can count failures
// without logging sensitive packet material.
enum TimeSyncResult {
  TIME_SYNC_OK,
  TIME_SYNC_DISABLED,
  TIME_SYNC_DISPLAY_NAME_MISMATCH,
  TIME_SYNC_MALFORMED,
  TIME_SYNC_SIGNATURE_INVALID
};

class TimeSyncAuth {
  // Strict decimal helpers are retained for canonical rendering symmetry and
  // any future CLI/test parsing, but packet parsing itself is binary-only.
  static bool parseUint32Strict(const char* text, size_t len, uint32_t& value);
  static bool parseUint16Strict(const char* text, size_t len, uint16_t& value);

  // Canonical signing helper: this is the only byte string accepted by the
  // verifier for a parsed timestamp/sequence/display-name/channel tuple.
  static bool buildCanonical(char* dest, size_t dest_len, const mesh::GroupChannel& channel,
                             const char* display_name, uint32_t timestamp, uint16_t sequence);
  static bool verifyParsed(const TimeSyncConfigView& config, uint32_t timestamp, uint16_t sequence,
                           const uint8_t signature[SIGNATURE_SIZE]);

public:
  // Configuration validators fail closed so wildcard-like setup cannot enable
  // the consumer.
  static bool isValidDisplayName(const char* name);
  static bool isValidPublicKey(const uint8_t key[PUB_KEY_SIZE]);

  // Status helpers expose only short diagnostics, never private key material.
  static void fingerprintLast3Hex(char dest[7], const uint8_t key[PUB_KEY_SIZE]);

  // Channel construction mirrors existing MeshCore group-channel conventions.
  static void configureHashtagChannel(mesh::GroupChannel& dest, const char* name);
  static void configureDefaultPublicChannel(mesh::GroupChannel& dest);

  // Binary-only parser for PAYLOAD_TYPE_GRP_DATA application type 0x0121.
  static TimeSyncResult parseAndVerifyBinary(const TimeSyncConfigView& config, const uint8_t* data,
                                             size_t data_len, TimeSyncMessage& msg);
};
