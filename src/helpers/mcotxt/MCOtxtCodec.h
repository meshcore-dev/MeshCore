#pragma once

#include <stddef.h>
#include <stdint.h>

// MCOtxt v1 stream codec: the bit stream of docs/MCOTXT_V1_PROTOCOL.md in the
// MeshCore Open Advanced repository (header, token tree, prediction contexts,
// case controls, UTF8_RUN, RAW_UTF8 mode). Pure C++, no allocation: every
// buffer belongs to the caller, and the stream is never self-terminating, so
// the exact bit count travels with it (see MCOtxtTransport for the frame).
namespace mcotxt {

enum class DecodeStatus : uint8_t {
  Ok,
  UnknownVersion,
  UnsupportedGeneration,
  UnknownLanguage,
  ModelUnavailable,
  UnsupportedHeader,
  InvalidToken,
  InvalidShift,
  InvalidUtf8,
  UnexpectedEnd,
  Malformed,
  OutputTooSmall,
};

struct DecodeResult {
  DecodeStatus status;
  bool raw_utf8;
  uint8_t language_a;   // kLanguageNone in RAW_UTF8 mode
  uint8_t language_b;   // kLanguageNone when absent
};

// Decodes [bit_length] bits of [data] into NUL-terminated UTF-8. [out_length]
// excludes the terminator; the capacity includes it. On OutputTooSmall the
// output holds the longest prefix of whole characters that fit, terminated,
// so a caller can still show how the text starts.
DecodeResult decodeStream(const uint8_t* data, size_t data_length, uint32_t bit_length,
                          char* out, size_t out_capacity, size_t& out_length);

// Strict UTF-8, the way the reference decoder's `utf8.decode(allowMalformed:
// false)` sees it: no overlongs, no surrogates, nothing past U+10FFFF.
bool utf8Decode(const uint8_t* data, size_t length, size_t& position, uint32_t& codepoint);
size_t utf8Encode(uint32_t codepoint, uint8_t* out);

}  // namespace mcotxt

