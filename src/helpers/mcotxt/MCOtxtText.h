#pragma once

#include <stddef.h>

namespace mcotxt {

// Decode complete mct: containers at word boundaries, including containers in
// surrounding prose. Unknown/malformed containers remain verbatim. False means
// no substitution, or insufficient workspace: the caller must keep the raw frame.
// A reply anchor becomes one leading @[Name] before multipart planning.
// When requested, mention_length reports the complete leading reply mention so
// companion splitting can keep it entirely in the first part.
bool decodeText(const char* input, char* output, size_t capacity, size_t* mention_length = nullptr);

}  // namespace mcotxt
