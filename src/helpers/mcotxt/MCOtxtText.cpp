#include "MCOtxtText.h"

#include <ctype.h>
#include <string.h>

#include "MCOtxtTransport.h"

namespace mcotxt {
namespace {

const char kBase91Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    "!#$%&()*+,./:;<=>?@[]^_`{|}~\"";
const size_t kMaxTokenBytes = MAX_FRAME_SIZE + 1;  // input frame plus NUL

bool isBoundary(const char* input, const char* position) {
  if (position == input) return true;
  const char* previous = position - 1;
  while (previous > input && ((uint8_t)*previous & 0xC0) == 0x80) --previous;
  const uint8_t lead = (uint8_t)*previous;
  if (lead < 0x80) return !isalnum(lead) && lead != '_';

  // Do not decode a suffix of a word, including the scripts in the models.
  uint32_t cp;
  size_t length;
  if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; length = 2; }
  else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; length = 3; }
  else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; length = 4; }
  else return false;
  if ((size_t)(position - previous) != length) return false;
  for (size_t i = 1; i < length; ++i) cp = (cp << 6) | ((uint8_t)previous[i] & 0x3F);
  const bool letter = (cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7) ||
                      (cp >= 0x100 && cp <= 0x24F) || (cp >= 0x370 && cp <= 0x52F);
  return !letter;
}

bool append(char* output, size_t capacity, size_t& length, const char* text, size_t count) {
  if (count >= capacity - length) return false;
  memcpy(output + length, text, count);
  length += count;
  output[length] = '\0';
  return true;
}

}  // namespace

bool decodeText(const char* input, char* output, size_t capacity, size_t* mention_length) {
  if (mention_length) *mention_length = 0;
  if (!input || !output || capacity == 0) return false;
  output[0] = '\0';
  size_t length = 0;
  bool changed = false;
  DecodedMessage reply;
  const char* plain = input;
  const char* cursor = input;
  while (*cursor) {
    if (strncmp(cursor, kTextPrefix, 4) != 0 || !isBoundary(input, cursor)) {
      ++cursor;
      continue;
    }
    const char* encoded = cursor + 4;
    const char* end = encoded;
    while (*end && strchr(kBase91Alphabet, *end)) ++end;
    if (end == encoded) { ++cursor; continue; }

    if (!append(output, capacity, length, plain, (size_t)(cursor - plain))) return false;
    const char* accepted = nullptr;
    char token[kMaxTokenBytes];
    // Quotes/brackets are also Base91 characters. Consume the longest complete
    // canonical, structurally valid container and leave punctuation untouched.
    for (const char* candidate = end; candidate > encoded; --candidate) {
      const size_t token_length = (size_t)(candidate - cursor);
      if (token_length >= sizeof(token)) continue;
      memcpy(token, cursor, token_length);
      token[token_length] = '\0';
      DecodedMessage message;
      const MessageStatus status = decodeTextPayload(token, output + length,
                                                     capacity - length, message);
      if (status == MessageStatus::TooLong) return false;  // never publish a clipped decode
      if (status != MessageStatus::Ok) continue;
      if (message.has_reply) {
        // One ordinary companion message has one leading mention. Distinct
        // reply anchors in several embedded containers cannot be merged without
        // losing their relationship; keep that unusual source raw instead.
        if (reply.has_reply && (strcmp(reply.reply_author, message.reply_author) != 0 ||
                                reply.reply_timestamp != message.reply_timestamp)) return false;
        if (!reply.has_reply) reply = message;  // fixed name arrays own their copies
      }
      accepted = candidate;
      break;
    }
    if (!accepted) {
      // Failed candidates may have written a partial result. Discard it; the
      // undecoded token stays in the next plain range.
      output[length] = '\0';
      plain = cursor;
      ++cursor;
      continue;
    }
    length += strlen(output + length);
    changed = true;
    plain = cursor = accepted;
  }
  if (!append(output, capacity, length, plain, strlen(plain))) return false;
  bool truncated = false;
  if (!changed || !ensureReplyMentionPrefix(reply, output, capacity, truncated) || truncated)
    return false;
  if (mention_length && reply.has_reply && reply.reply_author[0])
    *mention_length = strlen(reply.reply_author) + 3;
  return true;
}

}  // namespace mcotxt
