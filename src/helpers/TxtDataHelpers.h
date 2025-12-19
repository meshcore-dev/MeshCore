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
  static const char* ftoa(float f);
  static const char* ftoa3(float f); //Converts float to string with 3 decimal places
  static bool isBlank(const char* str);
  static uint32_t fromHex(const char* src);
};
