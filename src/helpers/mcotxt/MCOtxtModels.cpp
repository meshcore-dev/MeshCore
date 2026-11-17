#include <helpers/mcotxt/MCOtxtModels.h>

#include <string.h>

#include "generated/model_en.h"
#include "generated/model_ru.h"
#include "generated/model_fr.h"
#include "generated/model_de.h"
#include "generated/model_it.h"
#include "generated/model_uk.h"
#include "generated/model_be.h"

namespace mcotxt {
namespace {

#define MCOTXT_MODEL(lang)                                                     \
  {                                                                            \
    mcotxt_##lang##_language_id, mcotxt_##lang##_primary_count,                \
    mcotxt_##lang##_extension_count, mcotxt_##lang##_uppercase_count,          \
    mcotxt_##lang##_primary_symbols, mcotxt_##lang##_extension_symbols,        \
    mcotxt_##lang##_start_top4, mcotxt_##lang##_punct_start_top4,              \
    mcotxt_##lang##_top4, mcotxt_##lang##_uppercase_map,                       \
    mcotxt_##lang##_wire_hash                                                  \
  }

// Wire id order.
const Model kModels[kLanguageCount] = {
  MCOTXT_MODEL(en), MCOTXT_MODEL(ru), MCOTXT_MODEL(fr), MCOTXT_MODEL(de),
  MCOTXT_MODEL(it), MCOTXT_MODEL(uk), MCOTXT_MODEL(be),
};

#undef MCOTXT_MODEL

const char* const kLanguageCodes[kLanguageCount] = {
  "en", "ru", "fr", "de", "it", "uk", "be",
};

// Fixed for v1: SPACE, ASCII punctuation, typographic quotes and dashes, LF.
const uint16_t kPunctuation[kPunctuationCount] = {
  0x0020, 0x002E, 0x002C, 0x0021, 0x003F, 0x003A, 0x003B, 0x002D,
  0x2014, 0x005F, 0x0027, 0x0022, 0x00AB, 0x00BB, 0x201C, 0x201D,
  0x201E, 0x2018, 0x2019, 0x0028, 0x0029, 0x005B, 0x005D, 0x002F,
  0x005C, 0x0040, 0x0023, 0x0025, 0x0026, 0x002B, 0x003D, 0x000A,
};

}  // namespace

uint32_t Model::symbolAt(uint8_t index) const {
  if (index < primary_count) return primary[index];
  index = (uint8_t)(index - primary_count);
  if (index < extension_count) return extension[index];
  return 0;
}

int Model::indexOf(uint32_t codepoint) const {
  if (codepoint > 0xFFFF) return -1;
  for (uint8_t i = 0; i < primary_count; i++) {
    if (primary[i] == codepoint) return i;
  }
  for (uint8_t i = 0; i < extension_count; i++) {
    if (extension[i] == codepoint) return primary_count + i;
  }
  return -1;
}

int Model::indexOfUppercase(uint32_t codepoint) const {
  if (codepoint > 0xFFFF) return -1;
  for (uint8_t i = 0; i < uppercase_count; i++) {
    if (uppercase[i].uppercase_codepoint == codepoint) {
      return uppercase[i].lowercase_symbol_index;
    }
  }
  return -1;
}

uint32_t Model::uppercaseOf(uint8_t index) const {
  for (uint8_t i = 0; i < uppercase_count; i++) {
    if (uppercase[i].lowercase_symbol_index == index) {
      return uppercase[i].uppercase_codepoint;
    }
  }
  return 0;
}

const Model* modelFor(uint8_t language_id) {
  if (language_id >= kLanguageCount) return nullptr;
  return &kModels[language_id];
}

const char* languageCode(uint8_t language_id) {
  if (language_id >= kLanguageCount) return nullptr;
  return kLanguageCodes[language_id];
}

int languageIdForCode(const char* code) {
  if (code == nullptr || code[0] == '\0' || code[1] == '\0' || code[2] != '\0') return -1;
  const char a = (char)(code[0] | 0x20);
  const char b = (char)(code[1] | 0x20);
  for (uint8_t i = 0; i < kLanguageCount; i++) {
    if (kLanguageCodes[i][0] == a && kLanguageCodes[i][1] == b) return i;
  }
  return -1;
}

uint32_t punctuationAt(uint8_t id) {
  return id < kPunctuationCount ? kPunctuation[id] : 0;
}

int punctuationIdOf(uint32_t codepoint) {
  if (codepoint > 0xFFFF) return -1;
  for (uint8_t i = 0; i < kPunctuationCount; i++) {
    if (kPunctuation[i] == codepoint) return i;
  }
  return -1;
}

}  // namespace mcotxt

