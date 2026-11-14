#pragma once

#include <stddef.h>
#include <stdint.h>

// How a decoded MCOtxt text reaches an app that cannot decode it itself: as
// one plain message when it fits, else as numbered parts
//   "Name: <chunk>... [1/3]", "Name: ...<chunk>... [2/3]", "Name: ...<chunk> [3/3]"
// each within the byte budget of one message. The name is the channel's
// outer "Name: " layer and is absent for a direct message.
namespace mcotxt {

// Deliberate future headroom. Normal radio-sized messages produce 2-3 parts;
// 20 is not a target batch size and should be revisited together with
// MCOTXT_SCRATCH_BYTES and the companion offline queue if either grows.
static const size_t kMaxParts = 20;

struct TextPart {
  size_t offset;   // into the text
  size_t length;
};

// Splits [text] into at most [max_parts] parts so that every formatted part
// fits [max_bytes] once [prefix_length] bytes of "Name: " are in front. A
// chunk breaks after a space or line feed when one is in reach and keeps at
// least half the chunk, else on a character boundary; the break characters
// themselves are dropped. Returns the part count; 0 when the text needs more
// parts than [max_parts] or not even one character fits.
size_t splitForApp(const char* text, size_t text_length, size_t max_bytes,
                   size_t prefix_length, TextPart* parts, size_t max_parts);

// Writes part [index] of [count] as "<prefix><...><chunk><...> [i/n]" into [out],
// NUL-terminated. Leading dots mark a continuation; trailing dots mark another
// part to follow. A single part is "<prefix><chunk>" with no marks. [prefix]
// is the "Name: " layer, or empty. Returns the length, 0 when it does not fit.
size_t formatPart(const char* prefix, size_t prefix_length, const char* text,
                  const TextPart& part, size_t index, size_t count,
                  char* out, size_t out_capacity);

}  // namespace mcotxt

