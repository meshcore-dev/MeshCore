#pragma once

#include <stddef.h>
#include <stdint.h>

// The generated model headers define this pair type under the same guard; the
// definition here is the one every translation unit shares.
#ifndef MCOTXT_UPPERCASE_PAIR_T_DEFINED
#define MCOTXT_UPPERCASE_PAIR_T_DEFINED
#if defined(__GNUC__) || defined(__clang__)
#define MCOTXT_PACKED __attribute__((packed))
#else
#define MCOTXT_PACKED
#endif
typedef struct MCOTXT_PACKED {
  uint16_t uppercase_codepoint;
  uint8_t lowercase_symbol_index;
} mcotxt_uppercase_pair_t;
#endif

// MCOtxt v1 static language models, generation 0.
//
// The tables are the C headers the MeshCore Open Advanced trainer writes
// (tools/MCOtxt/generated/<lang>/model_<lang>.h in that repository), copied
// verbatim into generated/. Symbol order is wire format: it defines the
// literal ids, so the tables are never edited by hand and a regenerated table
// is a new model generation. The wire hash of every table is embedded and
// listed in the trainer's model_manifest.json.
namespace mcotxt {

static const uint8_t kCodecVersion = 1;
static const uint8_t kModelGeneration = 0;
static const uint8_t kLanguageCount = 7;
static const uint8_t kLanguageNone = 0xFF;   // global id: no language
static const uint8_t kPunctuationCount = 32;
static const uint8_t kPunctuationSpace = 0;
static const uint8_t kPunctuationLineFeed = 31;

struct Model {
  uint8_t language_id;
  uint8_t primary_count;
  uint8_t extension_count;
  uint8_t uppercase_count;
  const uint16_t* primary;
  const uint16_t* extension;
  const uint8_t* start_top4;
  const uint8_t* punct_start_top4;
  const uint8_t* top4;
  const mcotxt_uppercase_pair_t* uppercase;
  const char* wire_hash;

  uint8_t symbolCount() const { return (uint8_t)(primary_count + extension_count); }

  // Symbol index -> lowercase code point; indexes run over primary ++ extension.
  uint32_t symbolAt(uint8_t index) const;

  // Lowercase code point -> symbol index, or -1 when it is not a model symbol.
  int indexOf(uint32_t codepoint) const;

  // Uppercase code point -> the symbol index it lowercases to, or -1.
  int indexOfUppercase(uint32_t codepoint) const;

  // The uppercase form of a symbol, or 0 when it has none (digits, SPACE).
  uint32_t uppercaseOf(uint8_t index) const;

  // The four-entry TOP4 row of the SYMBOL(prev) context.
  const uint8_t* rowFor(uint8_t index) const { return top4 + (size_t)index * 4u; }
};

// Wire ids 0..6: en ru fr de it uk be. Null for any other id.
const Model* modelFor(uint8_t language_id);

// Two-letter code of a wire id, or null.
const char* languageCode(uint8_t language_id);

// Wire id for a two-letter code (case-insensitive), or -1.
int languageIdForCode(const char* code);

// The punctuation page shared by every language.
uint32_t punctuationAt(uint8_t id);
int punctuationIdOf(uint32_t codepoint);

}  // namespace mcotxt

