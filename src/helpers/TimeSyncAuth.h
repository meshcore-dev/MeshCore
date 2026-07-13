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
#define TIME_SYNC_MAX_SOURCES 4

// One configured time authority. The display name is a selector; the public
// key is the actual authentication control.
struct TimeSyncAuthorityConfig {
  uint8_t slot_index;
  const char* display_name;
  const uint8_t* public_key;
};

// Lightweight view over persisted repeater settings. It avoids copying the
// pinned public keys into packet-processing code.
struct TimeSyncConfigView {
  bool enabled;
  mesh::GroupChannel channel;
  uint8_t source_count;
  const TimeSyncAuthorityConfig* sources;
  // Legacy single-source fields are kept so existing callers can continue to
  // use the original parseAndVerifyBinary() API without building an array.
  const char* display_name;
  const uint8_t* public_key;
};

// Authenticated fields returned to the repeater clock policy after signature
// verification succeeds.
struct TimeSyncMessage {
  uint32_t timestamp;
  uint16_t sequence;
  uint8_t source_index;
};

// Parsed form of a repeater CLI authority-slot setting such as
// `source.1.display_name`. The field pointer aliases the caller's input.
struct TimeSyncSourceSettingKey {
  uint8_t source_index;
  const char* field;
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

// Result of applying one CLI source-slot setting to plain preference buffers.
enum TimeSyncSourceSettingResult {
  TIME_SYNC_SOURCE_SETTING_OK,
  TIME_SYNC_SOURCE_SETTING_UNKNOWN_FIELD,
  TIME_SYNC_SOURCE_SETTING_INVALID_DISPLAY_NAME,
  TIME_SYNC_SOURCE_SETTING_INVALID_PUBLIC_KEY,
  TIME_SYNC_SOURCE_SETTING_CLEAR_REQUIRES_YES
};

class TimeSyncAuth {
  // Canonical signing helper: this is the only byte string accepted by the
  // verifier for a parsed timestamp/sequence/display-name/channel tuple.
  static bool buildCanonical(char* dest, size_t dest_len, const mesh::GroupChannel& channel,
                             const char* display_name, uint32_t timestamp, uint16_t sequence);
  static bool verifyParsed(const mesh::GroupChannel& channel, const TimeSyncAuthorityConfig& source,
                           uint32_t timestamp, uint16_t sequence, const uint8_t signature[SIGNATURE_SIZE]);

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

  // Parse the shared source-slot CLI grammar used by get/set time_sync.source.N.
  static bool parseSourceSettingKey(const char* key, TimeSyncSourceSettingKey& parsed);

  // Apply one parsed source-slot CLI setting. Persistence and replay reset stay
  // with the firmware caller because those side effects are board-specific.
  static TimeSyncSourceSettingResult applySourceSetting(char* display_name, size_t display_name_len,
                                                       uint8_t public_key[PUB_KEY_SIZE],
                                                       const char* field, const char* value);

  // Binary-only parser for PAYLOAD_TYPE_GRP_DATA application type 0x0121.
  static TimeSyncResult parseAndVerifyBinaryMulti(const TimeSyncConfigView& config, const uint8_t* data,
                                                  size_t data_len, TimeSyncMessage& msg);
  static TimeSyncResult parseAndVerifyBinary(const TimeSyncConfigView& config, const uint8_t* data,
                                             size_t data_len, TimeSyncMessage& msg);
};
