// basE91 encode/decode (canonical 91-character alphabet).
// Algorithm by Joachim Henke, public domain (2000-2006).
// Reference: http://base91.sourceforge.net
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mesh {
namespace base91 {

// basE91 using the canonical 91-character alphabet.  Output capacities include
// the terminating NUL for encode(); decoded binary buffers are not terminated.
bool encode(const uint8_t* input, size_t input_length, char* output,
            size_t output_capacity, size_t& output_length);
bool decode(const char* input, size_t input_length, uint8_t* output,
            size_t output_capacity, size_t& output_length);

}  // namespace base91
}  // namespace mesh
