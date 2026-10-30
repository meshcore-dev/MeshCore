// basE91 encode/decode (canonical 91-character alphabet).
// Algorithm by Joachim Henke, public domain (2000-2006).
// Reference: http://base91.sourceforge.net
#include "Base91.h"

#include <string.h>

namespace mesh {
namespace base91 {
namespace {

constexpr char ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    "!#$%&()*+,./:;<=>?@[]^_`{|}~\"";

int valueOf(uint8_t ch) {
  if (ch == 0) return -1;
  const char* match = strchr(ALPHABET, static_cast<char>(ch));
  return match == nullptr ? -1 : static_cast<int>(match - ALPHABET);
}

bool append(char value, char* output, size_t capacity, size_t& length) {
  if (length + 1 >= capacity) return false;
  output[length++] = value;
  return true;
}

}  // namespace

bool encode(const uint8_t* input, size_t input_length, char* output,
            size_t output_capacity, size_t& output_length) {
  output_length = 0;
  if ((input == nullptr && input_length != 0) || output == nullptr || output_capacity == 0)
    return false;

  uint32_t queue = 0;
  unsigned bits = 0;
  for (size_t i = 0; i < input_length; ++i) {
    queue |= static_cast<uint32_t>(input[i]) << bits;
    bits += 8;
    if (bits > 13) {
      uint32_t value = queue & 8191U;
      if (value > 88U) {
        queue >>= 13;
        bits -= 13;
      } else {
        value = queue & 16383U;
        queue >>= 14;
        bits -= 14;
      }
      if (!append(ALPHABET[value % 91U], output, output_capacity, output_length) ||
          !append(ALPHABET[value / 91U], output, output_capacity, output_length)) {
        output_length = 0;
        output[0] = '\0';
        return false;
      }
    }
  }

  if (bits != 0) {
    if (!append(ALPHABET[queue % 91U], output, output_capacity, output_length) ||
        ((bits > 7 || queue > 90U) &&
         !append(ALPHABET[queue / 91U], output, output_capacity, output_length))) {
      output_length = 0;
      output[0] = '\0';
      return false;
    }
  }
  output[output_length] = '\0';
  return true;
}

bool decode(const char* input, size_t input_length, uint8_t* output,
            size_t output_capacity, size_t& output_length) {
  output_length = 0;
  if ((input == nullptr && input_length != 0) || output == nullptr) return false;

  uint32_t queue = 0;
  unsigned bits = 0;
  int pending = -1;
  for (size_t i = 0; i < input_length; ++i) {
    const int decoded = valueOf(static_cast<uint8_t>(input[i]));
    if (decoded < 0) return false;
    if (pending < 0) {
      pending = decoded;
      continue;
    }

    const uint32_t value = static_cast<uint32_t>(pending + decoded * 91);
    queue |= value << bits;
    bits += (value & 8191U) > 88U ? 13U : 14U;
    while (bits >= 8) {
      if (output_length >= output_capacity) return false;
      output[output_length++] = static_cast<uint8_t>(queue);
      queue >>= 8;
      bits -= 8;
    }
    pending = -1;
  }

  if (pending >= 0) {
    queue |= static_cast<uint32_t>(pending) << bits;
    if (output_length >= output_capacity) return false;
    output[output_length++] = static_cast<uint8_t>(queue);
  }
  return true;
}

}  // namespace base91
}  // namespace mesh
