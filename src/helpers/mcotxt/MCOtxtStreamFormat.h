#pragma once

#include <stdint.h>

#include <helpers/mcotxt/MCOtxtModels.h>

// The bit-level shape of an MCOtxt v1 stream: header field widths and escapes,
// the extended-header formats,
// the extended-control sub-opcodes, and the prediction context rules. Internal
// to the codec.
namespace mcotxt {

static const uint8_t kHeaderFieldBits = 3;
static const uint8_t kHeaderFieldEscape = 7;
static const uint8_t kLanguageBits = 3;
static const uint8_t kExtendedHeaderWireId = 7;      // language A field: extended header
static const uint8_t kLanguageNoneWireId = 7;        // language B field: none
static const uint8_t kExtendedPair8Format = 0;
static const uint8_t kRawUtf8Format = 1;
static const uint8_t kRawUtf8PaddingBits = 4;
static const uint8_t kSubopcodeSwitchOther = 0;
static const uint8_t kSubopcodeResetContext = 1;
static const uint8_t kSubopcodeUtf8Run = 2;
static const uint8_t kSubopcodeToggleCase = 3;
static const uint8_t kUtf8RunMaxBytes = 32;

enum class Context : uint8_t { Start, AfterPunct, Symbol };

// SPACE keeps a SYMBOL context and otherwise gives START; LF gives START;
// every other entry gives AFTER_PUNCT.
inline void contextAfterPunctuation(uint8_t id, Context& context) {
  if (id == kPunctuationSpace) {
    if (context != Context::Symbol) context = Context::Start;
  } else if (id == kPunctuationLineFeed) {
    context = Context::Start;
  } else {
    context = Context::AfterPunct;
  }
}

inline const uint8_t* rowForContext(const Model& model, Context context, uint8_t prev) {
  switch (context) {
    case Context::Start: return model.start_top4;
    case Context::AfterPunct: return model.punct_start_top4;
    default: return model.rowFor(prev);
  }
}

}  // namespace mcotxt

