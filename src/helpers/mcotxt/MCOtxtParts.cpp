#include <helpers/mcotxt/MCOtxtParts.h>

#include <stdio.h>
#include <string.h>

#include <helpers/mcotxt/MCOtxtCodec.h>

namespace mcotxt {
namespace {

size_t decimalWidth(size_t value) {
  size_t width = 1;
  while (value >= 10) {
    value /= 10;
    width++;
  }
  return width;
}

// " [i/n]": a space, a bracket, i, a slash, n, a bracket.
size_t markWidth(size_t digits) { return 4 + 2 * digits; }

bool isBreak(char c) { return c == ' ' || c == '\n'; }

// The longest prefix of [text] within [budget] bytes on a character boundary.
size_t fitBytes(const char* text, size_t length, size_t budget) {
  size_t position = 0, fit = 0;
  uint32_t codepoint;
  while (position < length && utf8Decode((const uint8_t*)text, length, position, codepoint) &&
         position <= budget) {
    fit = position;
  }
  return fit;
}

// Splits with marks of [digits] wide counts; returns the part count.
size_t splitWith(const char* text, size_t text_length, size_t max_bytes, size_t prefix_length,
                 size_t digits, TextPart* parts, size_t max_parts) {
  size_t count = 0, offset = 0;
  while (offset < text_length) {
    if (count == max_parts) return 0;
    const size_t reserve = prefix_length + (count > 0 ? 3 : 0) + markWidth(digits);
    if (reserve >= max_bytes) return 0;
    size_t budget = max_bytes - reserve;
    const size_t remaining = text_length - offset;
    // A final part needs no trailing marker and may use the full budget.
    // Otherwise reserve "..." before choosing a UTF-8-safe split point.
    if (remaining > budget) {
      if (budget <= 3) return 0;
      budget -= 3;
    }
    size_t take = fitBytes(text + offset, remaining, budget);
    if (take == 0) return 0;
    if (take < remaining) {
      // Prefer ending on a space or line feed when that keeps at least half
      // the chunk; a long word is cut rather than pushed whole to the next.
      size_t cut = take;
      while (cut > 0 && !isBreak(text[offset + cut - 1])) cut--;
      if (cut >= (take + 1) / 2) take = cut;
    }
    size_t length = take;
    while (length > 0 && isBreak(text[offset + length - 1])) length--;
    parts[count].offset = offset;
    parts[count].length = length;
    count++;
    offset += take;
    while (offset < text_length && isBreak(text[offset])) offset++;
  }
  return count;
}

}  // namespace

size_t splitForApp(const char* text, size_t text_length, size_t max_bytes,
                   size_t prefix_length, TextPart* parts, size_t max_parts) {
  if (text == nullptr || text_length == 0 || parts == nullptr || max_parts == 0) return 0;
  // Everything in one message needs no marks.
  if (prefix_length < max_bytes && text_length <= max_bytes - prefix_length) {
    parts[0].offset = 0;
    parts[0].length = text_length;
    return 1;
  }
  // The mark grows with the count, so plan with a width and confirm it.
  for (size_t digits = 1; digits <= decimalWidth(max_parts); digits++) {
    const size_t count = splitWith(text, text_length, max_bytes, prefix_length, digits, parts, max_parts);
    if (count == 0) return 0;
    if (decimalWidth(count) <= digits) return count;
  }
  return 0;
}

size_t formatPart(const char* prefix, size_t prefix_length, const char* text,
                  const TextPart& part, size_t index, size_t count,
                  char* out, size_t out_capacity) {
  if (out == nullptr || out_capacity == 0 || count == 0 || index >= count) return 0;
  size_t length = 0;
  auto append = [&](const char* bytes, size_t n) {
    if (length + n + 1 > out_capacity) return false;
    memcpy(out + length, bytes, n);
    length += n;
    return true;
  };
  if (prefix_length > 0 && !append(prefix, prefix_length)) return 0;
  if (count > 1 && index > 0 && !append("...", 3)) return 0;
  if (!append(text + part.offset, part.length)) return 0;
  if (count > 1 && index + 1 < count && !append("...", 3)) return 0;
  if (count > 1) {
    char mark[16];
    const int n = snprintf(mark, sizeof(mark), " [%u/%u]", (unsigned)(index + 1), (unsigned)count);
    if (n <= 0 || !append(mark, (size_t)n)) return 0;
  }
  out[length] = '\0';
  return length;
}

}  // namespace mcotxt

