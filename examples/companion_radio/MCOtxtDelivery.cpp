#include "MCOtxtDelivery.h"

#include <string.h>
#include <stdio.h>

#include <helpers/mcotxt/MCOtxtParts.h>
#include <helpers/mcotxt/MCOtxtText.h>
#include <helpers/mcotxt/MCOtxtTransport.h>

namespace {

// Existing companion codes, not new commands. Keep this adapter independent of
// Arduino/MyMesh so queue delivery and legacy/V3 layouts can be tested natively.
const uint8_t kContact = 7, kChannel = 8, kContactV3 = 16, kChannelV3 = 17, kData = 27;
const uint8_t kPlain = 0, kSignedPlain = 2;

// APP_START may omit cap= entirely. In each semicolon-delimited field, look for
// cap= first, then scan its comma-separated list for the exact mctxt item.
// Empty lists/items and unrelated capabilities do not enable this decoder.
bool hasCapability(const uint8_t* info, size_t length) {
  if (!info) return false;
  size_t pos = 0;
  while (pos < length && info[pos]) {
    const size_t start = pos;
    while (pos < length && info[pos] && info[pos] != ';') ++pos;
    if (pos - start >= 4 && memcmp(info + start, "cap=", 4) == 0) {
      size_t item = start + 4;
      while (item < pos) {
        size_t end = item;
        while (end < pos && info[end] != ',') ++end;
        if (end - item == 5 && memcmp(info + item, "mctxt", 5) == 0) return true;
        item = end + 1;
      }
    }
    if (pos < length && info[pos] == ';') ++pos;
  }
  return false;
}

struct Layout {
  size_t header;
  bool channel;
  bool v3;
};

bool textLayout(const uint8_t* frame, size_t length, Layout& layout) {
  switch (frame[0]) {
    case kContact:   layout = {13, false, false}; break;
    case kContactV3: layout = {16, false, true}; break;
    case kChannel:   layout = {8, true, false}; break;
    case kChannelV3: layout = {11, true, true}; break;
    default: return false;
  }
  if (length < layout.header) return false;
  const uint8_t type = frame[layout.header - 5];
  if (type != kPlain && type != kSignedPlain) return false;  // never rewrite CLI data
  if (!layout.channel && type == kSignedPlain) layout.header += 4;  // room author key
  return length >= layout.header;
}

// Only generated text is adapted to the current app version. A capable app gets
// its original frame byte-for-byte, including its original protocol layout.
size_t copyHeader(const uint8_t* source, const Layout& layout, uint8_t app_version,
                  uint8_t* header) {
  const bool v3 = app_version >= 3;
  size_t n = 0;
  header[n++] = layout.channel ? (v3 ? kChannelV3 : kChannel) : (v3 ? kContactV3 : kContact);
  if (v3) {
    for (size_t i = 1; i <= 3; ++i) header[n++] = layout.v3 ? source[i] : 0;
  }
  const size_t start = layout.v3 ? 4 : 1;
  memcpy(header + n, source + start, layout.header - start);
  return n + layout.header - start;
}

}  // namespace

void MCOtxtDelivery::resetMessage() {
  _original_sent = false;
  _part = 0;
  _timestamp = 0;
}

void MCOtxtDelivery::appStart(const uint8_t* app_info, size_t length) {
  _supports_mctxt = hasCapability(app_info, length);
  resetMessage();  // reconnect/new app restarts the retained raw head from part one
}

int MCOtxtDelivery::next(const uint8_t* source, size_t length, uint8_t app_version,
                        uint32_t now, uint8_t* output, bool& consume) {
  consume = true;
  if (!source || !output || length == 0 || length > kFrameSize) return 0;
  auto raw = [&]() -> int {
    if (_original_sent) return 0;
    memcpy(output, source, length);
    return (int)length;
  };
  if (_supports_mctxt) return raw();

  Layout layout;
  uint8_t header[20];  // V3 direct header including a signed-room author prefix
  size_t header_length = 0, prefix_length = 0, mention_length = 0;
  char prefix[mcotxt::kMaxNameBytes + 2];  // bounded sender plus ": "
  char* decoded = mcotxt::scratch();
  if (textLayout(source, length, layout)) {
    char original[kFrameSize + 1];
    const size_t text_length = length - layout.header;
    memcpy(original, source + layout.header, text_length);
    original[text_length] = '\0';
    // Do not silently lose trailing bytes hidden after an embedded NUL.
    if (strlen(original) != text_length) return raw();
    const char* body = original;
    if (layout.channel) {
      const char* separator = strstr(original, ": ");
      if (separator) {
        prefix_length = (size_t)(separator + 2 - original);
        if (prefix_length > sizeof(prefix)) return raw();  // prefix is counted, not NUL-terminated
        memcpy(prefix, original, prefix_length);
        body += prefix_length;
      }
    }
    if (!mcotxt::decodeText(body, decoded, mcotxt::kScratchBytes, &mention_length)) return raw();
    header_length = copyHeader(source, layout, app_version, header);
  } else if (source[0] == kData && length >= 9 && length == (size_t)9 + source[8]) {
    const uint16_t type = (uint16_t)source[6] | ((uint16_t)source[7] << 8);
    if (!mcotxt::isBinaryEnvelope(type, source + 9, source[8])) return raw();
    // Preserve binary delivery for apps that use GROUP_DATA for other purposes.
    // Even malformed/future MCOtxt is delivered raw once, without extra notices.
    if (!_original_sent) {
      _original_sent = true;
      _timestamp = now;  // stable inherited timestamp across all subsequent parts
      memcpy(output, source, length);
      consume = false;
      return (int)length;
    }
    mcotxt::DecodedMessage message;
    if (mcotxt::decodeBinaryEnvelope(type, source + 9, source[8], decoded,
                                    mcotxt::kScratchBytes, message) != mcotxt::MessageStatus::Ok)
      return 0;
    bool truncated = false;
    if (!mcotxt::ensureReplyMentionPrefix(message, decoded, mcotxt::kScratchBytes, truncated) ||
        truncated) return 0;
    if (message.has_reply && message.reply_author[0])
      mention_length = strlen(message.reply_author) + 3;
    header[header_length++] = app_version >= 3 ? kChannelV3 : kChannel;
    if (app_version >= 3) {
      header[header_length++] = source[1];
      header[header_length++] = 0;
      header[header_length++] = 0;
    }
    header[header_length++] = source[4];  // channel index
    header[header_length++] = source[5];  // original packed path metadata
    header[header_length++] = kPlain;
    const uint32_t timestamp = message.has_timestamp ? message.timestamp : _timestamp;
    for (size_t i = 0; i < 4; ++i) header[header_length++] = (uint8_t)(timestamp >> (8 * i));
    const char* sender = message.has_sender ? message.sender : "?";
    const size_t name_prefix = strlen(sender) + 2;
    memcpy(prefix, sender, name_prefix - 2);
    prefix[name_prefix - 2] = ':';
    prefix[name_prefix - 1] = ' ';
    prefix_length = name_prefix;
  } else {
    return raw();
  }

  const size_t budget = kFrameSize - header_length < kTextSize
      ? kFrameSize - header_length : kTextSize;
  mcotxt::TextPart parts[mcotxt::kMaxParts];
  size_t count = 0;
  if (decoded[0] == '\0') { parts[0] = {0, 0}; count = 1; }
  else count = mcotxt::splitForApp(decoded, strlen(decoded), budget, prefix_length,
                                  parts, mcotxt::kMaxParts);
  if (count == 0 || _part >= count) return raw();
  // Never split an exact author mention across frames. If it cannot coexist
  // with the sender/part marks, preserve the original instead of changing it.
  if (parts[0].length < mention_length) return raw();
  const size_t written = mcotxt::formatPart(prefix, prefix_length, decoded, parts[_part],
                                           _part, count, (char*)output + header_length,
                                           kFrameSize + 1 - header_length);
  // An empty direct message is valid; formatPart returns zero for it too.
  if (written > budget || (written == 0 && (prefix_length != 0 || decoded[0] != '\0')))
    return raw();
  memcpy(output, header, header_length);
  consume = ++_part >= count;
  return (int)(header_length + written);
}
