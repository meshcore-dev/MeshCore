#include <helpers/mcotxt/MCOtxtCodec.h>

#include <string.h>

#include <helpers/mcotxt/MCOtxtModels.h>
#include <helpers/mcotxt/MCOtxtStreamFormat.h>

namespace mcotxt {
namespace {

struct BitReader {
  const uint8_t* data;
  uint32_t bit_length;
  uint32_t position;

  uint32_t remaining() const { return bit_length - position; }

  bool readBits(uint8_t count, uint32_t& value) {
    if (count > remaining()) return false;
    value = 0;
    for (uint8_t i = 0; i < count; i++) {
      const uint32_t byte = position >> 3;
      const uint32_t bit = (data[byte] >> (7 - (position & 7))) & 1u;
      value = (value << 1) | bit;
      position++;
    }
    return true;
  }
};

struct Utf8Out {
  char* buf;
  size_t capacity;
  size_t length;

  bool put(uint32_t codepoint) {
    uint8_t tmp[4];
    const size_t n = utf8Encode(codepoint, tmp);
    if (n == 0 || length + n + 1 > capacity) return false;
    memcpy(buf + length, tmp, n);
    length += n;
    return true;
  }
};

DecodeResult fail(DecodeStatus status) {
  DecodeResult result;
  result.status = status;
  result.raw_utf8 = false;
  result.language_a = kLanguageNone;
  result.language_b = kLanguageNone;
  return result;
}

// Version and generation share one shape: 0..6 inline, 7 + eight bits of
// value - 7 above that. Every value has exactly one encoding.
bool readHeaderField(BitReader& reader, uint32_t& value) {
  if (!reader.readBits(kHeaderFieldBits, value)) return false;
  if (value != kHeaderFieldEscape) return true;
  uint32_t extra;
  if (!reader.readBits(8, extra)) return false;
  value = kHeaderFieldEscape + extra;
  return true;
}

bool validUtf8(const uint8_t* data, size_t length) {
  size_t position = 0;
  uint32_t codepoint;
  while (position < length) {
    if (!utf8Decode(data, length, position, codepoint)) return false;
  }
  return true;
}

size_t byteLengthForBits(uint32_t bit_length) {
  return (size_t)(bit_length / 8U) + (bit_length % 8U != 0U ? 1U : 0U);
}

}  // namespace

bool utf8Decode(const uint8_t* data, size_t length, size_t& position, uint32_t& codepoint) {
  if (position >= length) return false;
  const uint8_t b0 = data[position];
  if (b0 < 0x80) {
    codepoint = b0;
    position++;
    return true;
  }
  size_t need;
  uint32_t minimum;
  if ((b0 & 0xE0) == 0xC0) {
    need = 1; codepoint = b0 & 0x1F; minimum = 0x80;
  } else if ((b0 & 0xF0) == 0xE0) {
    need = 2; codepoint = b0 & 0x0F; minimum = 0x800;
  } else if ((b0 & 0xF8) == 0xF0) {
    need = 3; codepoint = b0 & 0x07; minimum = 0x10000;
  } else {
    return false;
  }
  if (length - position - 1 < need) return false;
  for (size_t i = 1; i <= need; i++) {
    const uint8_t b = data[position + i];
    if ((b & 0xC0) != 0x80) return false;
    codepoint = (codepoint << 6) | (b & 0x3F);
  }
  if (codepoint < minimum || codepoint > 0x10FFFF ||
      (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
    return false;
  }
  position += need + 1;
  return true;
}

size_t utf8Encode(uint32_t codepoint, uint8_t* out) {
  if (codepoint < 0x80) {
    out[0] = (uint8_t)codepoint;
    return 1;
  }
  if (codepoint < 0x800) {
    out[0] = (uint8_t)(0xC0 | (codepoint >> 6));
    out[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
    return 2;
  }
  if (codepoint < 0x10000) {
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return 0;
    out[0] = (uint8_t)(0xE0 | (codepoint >> 12));
    out[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
    out[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
    return 3;
  }
  if (codepoint > 0x10FFFF) return 0;
  out[0] = (uint8_t)(0xF0 | (codepoint >> 18));
  out[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
  out[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
  out[3] = (uint8_t)(0x80 | (codepoint & 0x3F));
  return 4;
}

DecodeResult decodeStream(const uint8_t* data, size_t data_length, uint32_t bit_length,
                          char* out, size_t out_capacity, size_t& out_length) {
  out_length = 0;
  if (out == nullptr || out_capacity == 0) return fail(DecodeStatus::OutputTooSmall);
  out[0] = '\0';
  if (data == nullptr && bit_length != 0) return fail(DecodeStatus::Malformed);
  if (byteLengthForBits(bit_length) > data_length) return fail(DecodeStatus::Malformed);

  BitReader reader = { data, bit_length, 0 };
  Utf8Out output = { out, out_capacity, 0 };
  // Overflow keeps the longest prefix of whole characters that fit, so a
  // caller can still show how the text starts.
  auto overflow = [&]() {
    out[output.length] = '\0';
    out_length = output.length;
    return fail(DecodeStatus::OutputTooSmall);
  };
  uint32_t value;

  if (!readHeaderField(reader, value)) return fail(DecodeStatus::UnexpectedEnd);
  if (value != kCodecVersion) return fail(DecodeStatus::UnknownVersion);
  uint32_t generation;
  if (!readHeaderField(reader, generation)) return fail(DecodeStatus::UnexpectedEnd);

  uint32_t language_a_field, language_b_field;
  if (!reader.readBits(kLanguageBits, language_a_field) ||
      !reader.readBits(kLanguageBits, language_b_field)) {
    return fail(DecodeStatus::UnexpectedEnd);
  }

  uint8_t language_a = kLanguageNone;
  uint8_t language_b = kLanguageNone;
  if (language_a_field == kExtendedHeaderWireId) {
    if (language_b_field == kRawUtf8Format) {
      // No tables involved: the generation is reported, never checked.
      uint32_t padding;
      if (!reader.readBits(kRawUtf8PaddingBits, padding)) return fail(DecodeStatus::UnexpectedEnd);
      if (padding != 0) return fail(DecodeStatus::InvalidUtf8);
      if (reader.remaining() % 8 != 0) return fail(DecodeStatus::InvalidUtf8);
      const size_t byte_count = reader.remaining() / 8;
      const size_t kept = byte_count + 1 > out_capacity ? out_capacity - 1 : byte_count;
      for (size_t i = 0; i < byte_count; i++) {
        uint32_t byte;
        reader.readBits(8, byte);
        if (i < kept) out[i] = (char)byte;
      }
      if (kept < byte_count) {
        // Overflow: keep the longest prefix of whole characters that fit.
        size_t prefix = 0, position = 0;
        uint32_t codepoint;
        while (position < kept && utf8Decode((const uint8_t*)out, kept, position, codepoint)) {
          prefix = position;
        }
        out[prefix] = '\0';
        out_length = prefix;
        DecodeResult result = fail(DecodeStatus::OutputTooSmall);
        result.raw_utf8 = true;
        return result;
      }
      if (!validUtf8((const uint8_t*)out, byte_count)) return fail(DecodeStatus::InvalidUtf8);
      out[byte_count] = '\0';
      out_length = byte_count;
      DecodeResult result = fail(DecodeStatus::Ok);
      result.raw_utf8 = true;
      return result;
    }
    if (language_b_field != kExtendedPair8Format) return fail(DecodeStatus::UnsupportedHeader);
    uint32_t global_a, global_b;
    if (!reader.readBits(8, global_a) || !reader.readBits(8, global_b)) {
      return fail(DecodeStatus::UnexpectedEnd);
    }
    if (global_a == kLanguageNone || global_a >= kLanguageCount) return fail(DecodeStatus::UnknownLanguage);
    if (global_b != kLanguageNone && global_b >= kLanguageCount) return fail(DecodeStatus::UnknownLanguage);
    language_a = (uint8_t)global_a;
    language_b = (uint8_t)global_b;
  } else {
    language_a = (uint8_t)language_a_field;
    language_b = language_b_field == kLanguageNoneWireId ? kLanguageNone : (uint8_t)language_b_field;
  }

  if (generation != kModelGeneration) return fail(DecodeStatus::UnsupportedGeneration);
  if (modelFor(language_a) == nullptr) return fail(DecodeStatus::ModelUnavailable);
  if (language_b != kLanguageNone) {
    if (language_b == language_a) return fail(DecodeStatus::UnknownLanguage);
    if (modelFor(language_b) == nullptr) return fail(DecodeStatus::ModelUnavailable);
  }

  uint8_t current = language_a;
  Context context = Context::Start;
  uint8_t prev = 0;
  bool shift = false;
  bool caps = false;

  while (reader.remaining() > 0) {
    uint32_t bit;
    // A language symbol from a TOP4 rank, a PRIMARY or an EXTENSION literal.
    int symbol = -1;

    if (!reader.readBits(1, bit)) return fail(DecodeStatus::UnexpectedEnd);
    if (bit == 0) {
      // TOP4: 00, 010, 0110, 0111.
      uint32_t rank = 0;
      if (!reader.readBits(1, bit)) return fail(DecodeStatus::UnexpectedEnd);
      if (bit != 0) {
        if (!reader.readBits(1, bit)) return fail(DecodeStatus::UnexpectedEnd);
        if (bit == 0) {
          rank = 1;
        } else {
          if (!reader.readBits(1, bit)) return fail(DecodeStatus::UnexpectedEnd);
          rank = bit == 0 ? 2 : 3;
        }
      }
      const Model* model = modelFor(current);
      symbol = rowForContext(*model, context, prev)[rank];
      if (symbol >= model->symbolCount()) return fail(DecodeStatus::InvalidToken);
    } else {
      if (!reader.readBits(1, bit)) return fail(DecodeStatus::UnexpectedEnd);
      if (bit == 0) {
        // PRIMARY literal.
        uint32_t id;
        if (!reader.readBits(5, id)) return fail(DecodeStatus::UnexpectedEnd);
        const Model* model = modelFor(current);
        if (id >= model->primary_count) return fail(DecodeStatus::InvalidToken);
        symbol = (int)id;
      } else {
        if (!reader.readBits(1, bit)) return fail(DecodeStatus::UnexpectedEnd);
        if (bit == 0) {
          // PUNCTUATION.
          if (shift) return fail(DecodeStatus::InvalidShift);
          uint32_t id;
          if (!reader.readBits(5, id)) return fail(DecodeStatus::UnexpectedEnd);
          if (id >= kPunctuationCount) return fail(DecodeStatus::InvalidToken);
          if (!output.put(punctuationAt((uint8_t)id))) return overflow();
          contextAfterPunctuation((uint8_t)id, context);
          continue;
        }
        if (!reader.readBits(1, bit)) return fail(DecodeStatus::UnexpectedEnd);
        if (bit == 0) {
          // EXTENSION literal.
          uint32_t id;
          if (!reader.readBits(5, id)) return fail(DecodeStatus::UnexpectedEnd);
          const Model* model = modelFor(current);
          if (id >= model->extension_count) return fail(DecodeStatus::InvalidToken);
          symbol = model->primary_count + (int)id;
        } else {
          if (!reader.readBits(1, bit)) return fail(DecodeStatus::UnexpectedEnd);
          if (bit == 0) {
            // SHIFT.
            if (shift) return fail(DecodeStatus::InvalidShift);
            shift = true;
            continue;
          }
          if (!reader.readBits(1, bit)) return fail(DecodeStatus::UnexpectedEnd);
          if (shift) return fail(DecodeStatus::InvalidShift);
          if (bit == 0) {
            // TOGGLE_LANGUAGE.
            if (language_b == kLanguageNone) return fail(DecodeStatus::InvalidToken);
            if (current == language_a) {
              current = language_b;
            } else if (current == language_b) {
              current = language_a;
            } else {
              return fail(DecodeStatus::InvalidToken);
            }
            context = Context::Start;
            continue;
          }
          // Extended control.
          uint32_t subopcode;
          if (!reader.readBits(3, subopcode)) return fail(DecodeStatus::UnexpectedEnd);
          switch (subopcode) {
            case kSubopcodeSwitchOther: {
              uint32_t id;
              if (!reader.readBits(8, id)) return fail(DecodeStatus::UnexpectedEnd);
              if (id == kLanguageNone || id >= kLanguageCount) return fail(DecodeStatus::InvalidToken);
              if (modelFor((uint8_t)id) == nullptr) return fail(DecodeStatus::ModelUnavailable);
              current = (uint8_t)id;
              context = Context::Start;
              continue;
            }
            case kSubopcodeResetContext:
              context = Context::Start;
              continue;
            case kSubopcodeUtf8Run: {
              uint32_t count;
              if (!reader.readBits(5, count)) return fail(DecodeStatus::UnexpectedEnd);
              count += 1;
              uint8_t bytes[kUtf8RunMaxBytes];
              for (uint32_t i = 0; i < count; i++) {
                uint32_t byte;
                if (!reader.readBits(8, byte)) return fail(DecodeStatus::UnexpectedEnd);
                bytes[i] = (uint8_t)byte;
              }
              if (!validUtf8(bytes, count)) return fail(DecodeStatus::InvalidUtf8);
              size_t position = 0;
              while (position < count) {
                uint32_t codepoint;
                utf8Decode(bytes, count, position, codepoint);   // validated above
                if (!output.put(codepoint)) return overflow();
              }
              context = Context::Start;
              continue;
            }
            case kSubopcodeToggleCase:
              // Case mode never touches the prediction context.
              caps = !caps;
              continue;
            default:
              return fail(DecodeStatus::InvalidToken);
          }
        }
      }
    }

    // Emit a language symbol; the context follows the lowercase table entry
    // whatever case was output.
    const Model* model = modelFor(current);
    const uint32_t lower = model->symbolAt((uint8_t)symbol);
    const uint32_t upper = model->uppercaseOf((uint8_t)symbol);
    if (shift && upper == 0) return fail(DecodeStatus::InvalidShift);
    const bool make_upper = upper != 0 && (caps != shift);
    if (!output.put(make_upper ? upper : lower)) return overflow();
    context = Context::Symbol;
    prev = (uint8_t)symbol;
    shift = false;
  }

  if (shift) return fail(DecodeStatus::InvalidShift);
  out[output.length] = '\0';
  out_length = output.length;
  DecodeResult result = fail(DecodeStatus::Ok);
  result.language_a = language_a;
  result.language_b = language_b;
  return result;
}

}  // namespace mcotxt

