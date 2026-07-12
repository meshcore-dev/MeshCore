#include "TimeSyncAuth.h"

#include <Identity.h>
#include <Utils.h>
#include <stdio.h>
#include <string.h>

// This is the documented MeshCore default public-channel 128-bit group key.
// It is the same public key listed in docs/faq.md for the default public
// channel, decoded from base64 `izOH6cXN6mrJ5e26oRXNcg==`.
static const uint8_t DEFAULT_PUBLIC_CHANNEL_SECRET[16] = {
  0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
  0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72
};

static char lowerHexNibble(uint8_t value) {
  // Keep only the low nibble so callers can pass either half of a byte.
  value &= 0x0F;

  // Return canonical lowercase ASCII hex for the nibble.
  return value < 10 ? (char)('0' + value) : (char)('a' + value - 10);
}

static void bytesToLowerHex(char* dest, const uint8_t* src, size_t len) {
  // Encode every byte as two lowercase hex characters without allocating.
  while (len--) {
    uint8_t value = *src++;
    *dest++ = lowerHexNibble(value >> 4);
    *dest++ = lowerHexNibble(value);
  }

  // Null terminate because all current callers use the result as C text.
  *dest = 0;
}

bool TimeSyncAuth::parseUint32Strict(const char* text, size_t len, uint32_t& value) {
  // Empty numeric fields are not valid decimal numbers.
  if (len == 0) return false;

  uint32_t acc = 0;
  for (size_t i = 0; i < len; i++) {
    // Accept decimal digits only; signs, prefixes and whitespace are rejected.
    char ch = text[i];
    if (ch < '0' || ch > '9') return false;

    // Check overflow before multiplying by ten and adding the next digit.
    uint32_t digit = (uint32_t)(ch - '0');
    if (acc > (UINT32_MAX - digit) / 10UL) return false;
    acc = acc * 10UL + digit;
  }

  // Return the parsed value only after the whole field has been validated.
  value = acc;
  return true;
}

bool TimeSyncAuth::parseUint16Strict(const char* text, size_t len, uint16_t& value) {
  // Parse with the wider helper first so overflow handling is centralised.
  uint32_t tmp;
  if (!parseUint32Strict(text, len, tmp) || tmp > UINT16_MAX) return false;

  // Narrow only after proving the value fits the wire sequence field.
  value = (uint16_t)tmp;
  return true;
}

bool TimeSyncAuth::buildCanonical(char* dest, size_t dest_len, const mesh::GroupChannel& channel,
                                  const char* display_name, uint32_t timestamp, uint16_t sequence) {
  // Bind the signature to a collision-resistant identifier for the configured
  // channel. PATH_HASH_SIZE is only one byte, so it is not strong enough for
  // signature scope; use SHA-256 over the full raw channel secret instead.
  uint8_t channel_id[32];
  char channel_id_hex[sizeof(channel_id) * 2 + 1];
  mesh::Utils::sha256(channel_id, sizeof(channel_id), channel.secret, 16);
  bytesToLowerHex(channel_id_hex, channel_id, sizeof(channel_id));

  // Build the exact ASCII bytes that both sender and receiver must sign.
  int written = snprintf(dest, dest_len, "%s\n%s\n%s\n%lu\n%u\n",
                         TIME_SYNC_CANONICAL_DOMAIN,
                         channel_id_hex,
                         display_name,
                         (unsigned long)timestamp,
                         (unsigned int)sequence);

  // Reject truncation so a shortened canonical string is never verified.
  return written > 0 && (size_t)written < dest_len;
}

bool TimeSyncAuth::verifyParsed(const TimeSyncConfigView& config, uint32_t timestamp, uint16_t sequence,
                                const uint8_t signature[SIGNATURE_SIZE]) {
  // Reconstruct the canonical message from local configuration and parsed fields.
  char canonical[TIME_SYNC_CANONICAL_MAX_LEN];
  if (!buildCanonical(canonical, sizeof(canonical), config.channel, config.display_name, timestamp, sequence)) {
    return false;
  }

  // Verify against the exact pinned authority public key from configuration.
  mesh::Identity authority(config.public_key);
  return authority.verify(signature, (const uint8_t*)canonical, strlen(canonical));
}

bool TimeSyncAuth::isValidDisplayName(const char* name) {
  // Require one explicit display name; empty names would act like wildcards.
  if (name == NULL || name[0] == 0) return false;

  // Enforce signed-byte safety and the binary payload length budget.
  size_t len = 0;
  while (name[len]) {
    if (name[len] == '\r' || name[len] == '\n') return false;
    len++;
    if (len > TIME_SYNC_MAX_DISPLAY_NAME_LEN) return false;
  }

  return true;
}

bool TimeSyncAuth::isValidPublicKey(const uint8_t key[PUB_KEY_SIZE]) {
  // Track the two wildcard-like public keys that must never be accepted.
  bool any_non_zero = false;
  bool any_not_ff = false;

  // Inspect the full 32-byte key; no prefix matching is allowed here.
  for (size_t i = 0; i < PUB_KEY_SIZE; i++) {
    if (key[i] != 0x00) any_non_zero = true;
    if (key[i] != 0xFF) any_not_ff = true;
  }

  // Accept only keys that are neither all-zero nor all-0xFF.
  return any_non_zero && any_not_ff;
}

void TimeSyncAuth::fingerprintLast3Hex(char dest[7], const uint8_t key[PUB_KEY_SIZE]) {
  // Hash the full public key before deriving the short diagnostic fingerprint.
  uint8_t digest[32];
  mesh::Utils::sha256(digest, sizeof(digest), key, PUB_KEY_SIZE);

  // Expose only the last three digest bytes, encoded as six lowercase hex chars.
  bytesToLowerHex(dest, &digest[sizeof(digest) - 3], 3);
}

void TimeSyncAuth::configureHashtagChannel(mesh::GroupChannel& dest, const char* name) {
  // Start from a known empty channel so unused secret bytes are zeroed.
  memset(&dest, 0, sizeof(dest));

  // MeshCore hashtag channels use the first 16 bytes of SHA-256("#name").
  uint8_t digest[32];
  mesh::Utils::sha256(digest, sizeof(digest), (const uint8_t*)name, strlen(name));
  memcpy(dest.secret, digest, 16);

  // MeshCore channel matching uses the hash of the shared channel secret.
  mesh::Utils::sha256(dest.hash, sizeof(dest.hash), dest.secret, 16);
}

void TimeSyncAuth::configureDefaultPublicChannel(mesh::GroupChannel& dest) {
  // Start from a known empty channel so unused secret bytes are zeroed.
  memset(&dest, 0, sizeof(dest));

  // Copy the documented default public-channel secret into the group channel.
  memcpy(dest.secret, DEFAULT_PUBLIC_CHANNEL_SECRET, sizeof(DEFAULT_PUBLIC_CHANNEL_SECRET));

  // MeshCore channel matching uses the hash of the shared channel secret.
  mesh::Utils::sha256(dest.hash, sizeof(dest.hash), dest.secret, sizeof(DEFAULT_PUBLIC_CHANNEL_SECRET));
}

TimeSyncResult TimeSyncAuth::parseAndVerifyBinary(const TimeSyncConfigView& config, const uint8_t* data,
                                                  size_t data_len, TimeSyncMessage& msg) {
  // Fail closed before parsing if the feature or required authority config is absent.
  if (!config.enabled) return TIME_SYNC_DISABLED;
  if (!isValidDisplayName(config.display_name) || !isValidPublicKey(config.public_key)) return TIME_SYNC_DISABLED;

  // Enforce the minimum fixed fields plus the 64-byte raw signature.
  if (data_len < 3 + 4 + 2 + 1 + SIGNATURE_SIZE) return TIME_SYNC_MALFORMED;

  // Require the exact binary marker before consuming binary numeric fields.
  if (memcmp(data, TIME_SYNC_BINARY_MARKER, 3) != 0) return TIME_SYNC_MALFORMED;

  // Decode the little-endian Unix timestamp from the binary payload.
  size_t pos = 3;
  uint32_t timestamp = ((uint32_t)data[pos]) |
                       ((uint32_t)data[pos + 1] << 8) |
                       ((uint32_t)data[pos + 2] << 16) |
                       ((uint32_t)data[pos + 3] << 24);
  pos += 4;

  // Decode the little-endian uint16 monotonic sequence number.
  uint16_t sequence = ((uint16_t)data[pos]) | ((uint16_t)data[pos + 1] << 8);
  pos += 2;

  // Validate and compare the exact display-name field before signature work.
  uint8_t display_len = data[pos++];
  if (display_len == 0 || display_len > TIME_SYNC_MAX_DISPLAY_NAME_LEN) return TIME_SYNC_MALFORMED;
  if (pos + display_len + SIGNATURE_SIZE != data_len) return TIME_SYNC_MALFORMED;
  if (strlen(config.display_name) != display_len || memcmp(&data[pos], config.display_name, display_len) != 0) {
    return TIME_SYNC_DISPLAY_NAME_MISMATCH;
  }
  pos += display_len;

  // Authenticate the canonical fields using the raw signature at the tail.
  if (!verifyParsed(config, timestamp, sequence, &data[pos])) return TIME_SYNC_SIGNATURE_INVALID;

  // Return the authenticated timestamp and sequence to the repeater policy code.
  msg.timestamp = timestamp;
  msg.sequence = sequence;
  return TIME_SYNC_OK;
}
