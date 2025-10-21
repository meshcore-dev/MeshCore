#pragma once

#include <stddef.h>
#include <stdint.h>

#define TXT_TYPE_PLAIN          0    // a plain text message
#define TXT_TYPE_CLI_DATA       1    // a CLI command
#define TXT_TYPE_SIGNED_PLAIN   2    // plain text, signed by sender

class StrHelper {
public:
  static void strncpy(char* dest, const char* src, size_t buf_sz);
  static void strzcpy(char* dest, const char* src, size_t buf_sz);   // pads with trailing nulls
  // Returns true if all characters are printable ASCII (0x20-0x7E)
  static bool isPrintableASCII(const char* s);
  // Copy src to dest, replacing non-printable ASCII bytes with '_'. Always null-terminates.
  static void filterToPrintableASCII(char* dest, const char* src, size_t buf_sz);
  static const char* ftoa(float f);
};
