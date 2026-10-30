#include "MCOtxtDisplay.h"

#include <string.h>

#include <helpers/UTF8Helpers.h>
#include "MCOtxtText.h"
#include "MCOtxtTransport.h"

namespace mcotxt {

const char* displayText(const char* text, bool channel) {
  if (!text) return text;
  size_t prefix_length = 0;
  if (channel) {
    const char* separator = strstr(text, ": ");
    if (separator) prefix_length = (size_t)(separator + 2 - text);
  }
  if (prefix_length >= kScratchBytes) return text;
  char* output = scratch();
  if (!decodeText(text + prefix_length, output + prefix_length,
                  kScratchBytes - prefix_length)) return text;
  memcpy(output, text, prefix_length);
  return output;
}

const char* displayBinary(uint16_t data_type, const uint8_t* data, size_t length) {
  char* output = scratch();
  DecodedMessage message;
  if (decodeBinaryEnvelope(data_type, data, length, output, kScratchBytes, message)
      != MessageStatus::Ok) return nullptr;
  bool truncated = false;
  if (!ensureReplyMentionPrefix(message, output, kScratchBytes, truncated) || truncated)
    return nullptr;

  // Reuse the ordinary channel preview: "Name: @[Reply] decoded text". The
  // binary callback still receives the original data separately, once.
  return prependSenderName(message, output, kScratchBytes) ? output : nullptr;
}

void copyDisplayPreview(char* output, size_t capacity, const char* text) {
  if (!output || capacity == 0) return;
  const size_t keep = mesh::validUtf8PrefixLength(text, capacity - 1);
  if (keep) memcpy(output, text, keep);
  output[keep] = '\0';
}

}  // namespace mcotxt
