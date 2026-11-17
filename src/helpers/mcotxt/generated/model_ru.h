/* GENERATED FILE - DO NOT EDIT BY HAND. */
/* MCOtxt v1 static TOP-4 model (START + AFTER_PUNCT): RU (wire id 1). */
#ifndef MCOTXT_MODEL_RU_H
#define MCOTXT_MODEL_RU_H

#include <stdint.h>

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

static const uint8_t mcotxt_ru_available = 1u;
static const char mcotxt_ru_wire_hash[] = "d123c4978a635bf1b26fe37261b7e96a0206b3ba15b2a80542e560f00d1eb193";
static const uint8_t mcotxt_ru_language_id = 1u;
static const uint8_t mcotxt_ru_primary_count = 32u;
static const uint8_t mcotxt_ru_extension_count = 12u;
static const uint8_t mcotxt_ru_symbol_count = 44u;
static const uint8_t mcotxt_ru_uppercase_count = 33u;

static const uint16_t mcotxt_ru_primary_symbols[32] = {
  0x0020u, 0x0441u, 0x043Cu, 0x0443u, 0x0440u, 0x043Au, 0x0435u, 0x043Bu, 0x043Du, 0x0438u,
  0x044Fu, 0x0442u, 0x0431u, 0x0434u, 0x0432u, 0x0433u, 0x0447u, 0x0430u, 0x0437u, 0x043Eu,
  0x044Bu, 0x043Fu, 0x0439u, 0x0445u, 0x0436u, 0x0448u, 0x044Eu, 0x044Cu, 0x044Du, 0x0449u,
  0x0446u, 0x0444u,
};

static const uint16_t mcotxt_ru_extension_symbols[12] = {
  0x0031u, 0x0032u, 0x0033u, 0x0451u, 0x0034u, 0x0035u, 0x0038u, 0x0037u, 0x0036u, 0x0039u,
  0x0030u, 0x044Au,
};

static const uint8_t mcotxt_ru_start_top4[4] = {
  21u, 8u, 0u, 13u,
};

static const uint8_t mcotxt_ru_punct_start_top4[4] = {
  0u, 11u, 32u, 5u,
};

static const uint8_t mcotxt_ru_top4[176] = {
  8u, 21u, 1u, 14u, 11u, 6u, 0u, 19u, 0u, 6u, 19u, 17u, 0u, 11u, 13u, 24u, 19u, 17u, 6u, 9u, 19u,
  17u, 0u, 9u, 0u, 11u, 8u, 4u, 9u, 27u, 19u, 6u, 17u, 19u, 6u, 9u, 0u, 11u, 8u, 7u, 0u, 11u, 18u,
  7u, 19u, 17u, 0u, 27u, 19u, 4u, 20u, 6u, 17u, 19u, 6u, 9u, 0u, 6u, 17u, 19u, 19u, 13u, 17u, 9u,
  6u, 11u, 17u, 9u, 0u, 7u, 5u, 11u, 17u, 0u, 8u, 9u, 0u, 11u, 13u, 14u, 0u, 22u, 25u, 7u, 19u, 4u,
  9u, 17u, 0u, 11u, 16u, 5u, 19u, 0u, 17u, 3u, 6u, 8u, 13u, 9u, 6u, 9u, 17u, 5u, 0u, 11u, 16u, 1u,
  0u, 8u, 5u, 1u, 11u, 5u, 8u, 7u, 6u, 17u, 35u, 9u, 9u, 6u, 17u, 0u, 9u, 19u, 7u, 6u, 0u, 42u,
  32u, 36u, 0u, 42u, 37u, 33u, 0u, 42u, 34u, 33u, 0u, 11u, 2u, 7u, 0u, 42u, 34u, 37u, 0u, 42u, 37u,
  40u, 0u, 40u, 42u, 33u, 0u, 39u, 42u, 37u, 0u, 37u, 38u, 42u, 0u, 41u, 42u, 37u, 0u, 42u, 2u, 5u,
  6u, 10u, 35u, 9u,
};

static const mcotxt_uppercase_pair_t mcotxt_ru_uppercase_map[33] = {
  { 0x0401u, 35u }, /* Ё -> ё */
  { 0x0410u, 17u }, /* А -> а */
  { 0x0411u, 12u }, /* Б -> б */
  { 0x0412u, 14u }, /* В -> в */
  { 0x0413u, 15u }, /* Г -> г */
  { 0x0414u, 13u }, /* Д -> д */
  { 0x0415u, 6u }, /* Е -> е */
  { 0x0416u, 24u }, /* Ж -> ж */
  { 0x0417u, 18u }, /* З -> з */
  { 0x0418u, 9u }, /* И -> и */
  { 0x0419u, 22u }, /* Й -> й */
  { 0x041Au, 5u }, /* К -> к */
  { 0x041Bu, 7u }, /* Л -> л */
  { 0x041Cu, 2u }, /* М -> м */
  { 0x041Du, 8u }, /* Н -> н */
  { 0x041Eu, 19u }, /* О -> о */
  { 0x041Fu, 21u }, /* П -> п */
  { 0x0420u, 4u }, /* Р -> р */
  { 0x0421u, 1u }, /* С -> с */
  { 0x0422u, 11u }, /* Т -> т */
  { 0x0423u, 3u }, /* У -> у */
  { 0x0424u, 31u }, /* Ф -> ф */
  { 0x0425u, 23u }, /* Х -> х */
  { 0x0426u, 30u }, /* Ц -> ц */
  { 0x0427u, 16u }, /* Ч -> ч */
  { 0x0428u, 25u }, /* Ш -> ш */
  { 0x0429u, 29u }, /* Щ -> щ */
  { 0x042Au, 43u }, /* Ъ -> ъ */
  { 0x042Bu, 20u }, /* Ы -> ы */
  { 0x042Cu, 27u }, /* Ь -> ь */
  { 0x042Du, 28u }, /* Э -> э */
  { 0x042Eu, 26u }, /* Ю -> ю */
  { 0x042Fu, 10u }, /* Я -> я */
};

#endif /* MCOTXT_MODEL_RU_H */
